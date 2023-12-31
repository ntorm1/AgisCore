#include "Hydra.h"
#pragma once
#include "pch.h" 

#include "Utils.h"
#include "Hydra.h"
#include "ExchangeMap.h"
#include "Portfolio.h"

#include "Broker/Broker.Base.h"

using namespace rapidjson;
using namespace Agis;

struct HydraPrivate
{
    HydraPrivate() :
        brokers(&this->exchanges),
        router(&this->exchanges, &this->brokers, &this->portfolios)
    {}
    BrokerMap brokers;
    ExchangeMap exchanges;
    AgisRouter router;
    PortfolioMap portfolios;
};

//============================================================================
Hydra::Hydra(int logging_, bool init_lua_state):
    p(new HydraPrivate())
{
    this->logging = logging_;
#ifdef USE_LUAJIT
    if (init_lua_state) {
        this->lua = new sol::state();
        init_lua_interface(this->lua);
    }
#else
    if (init_lua_state) {
		AGIS_THROW("Lua not enabled");
	}
#endif
}


//============================================================================
Hydra::~Hydra()
{
    #ifdef USE_LUAJIT
    // destruct strategies before lua state to prevent segfault. When strategies are destructed
    // they rely on the lua state to be valid.
    this->strategies.__clear();
    if (this->lua) {
        delete this->lua;
    }
	#endif
    delete this->p;
}


//============================================================================
void Hydra::__step()
{
    // step assets and exchanges forward in time
    this->p->exchanges.step();
    auto& expired_index_list = this->p->exchanges.__get_expired_index_list();
    
    // evaluate the portfolios at the current prices
    this->p->portfolios.__evaluate(true, true);

    // process strategy logic at end of each time step
    bool step;
    AGIS_TRY(step = this->strategies.__next());
    if (step) { this->p->router.__process(); };

    // process orders on the exchange and route to their portfolios on fill
    this->p->exchanges.__process_orders(this->p->router, true);
    this->p->router.__process();

    // evaluate the portfolios on close, then remove any position for assets that are expiring.
    AGIS_TRY(this->p->portfolios.__evaluate(true));
    this->p->portfolios.__on_assets_expired(this->p->router, expired_index_list);
    this->p->router.__process();
}



//============================================================================
AGIS_API std::expected<bool, AgisException> Hydra::__run()
{
    if (!this->is_built) 
    {
        auto res = this->build();
        if (!res.has_value()) return res;
        this->is_built = true; 
    }
    
    try {
        this->__reset();
    }
    catch (std::exception& e) {
		return std::unexpected<AgisException>(AGIS_EXCEP(e.what()));
	}
    
    auto index = this->p->exchanges.__get_dt_index(false);
    for (size_t i = this->current_index; i < index.size(); i++)
    {
        AGIS_TRY_RESULT(this->__step(), bool);
        this->current_index++;
    }
    return this->__cleanup();
}


//============================================================================
AgisResult<bool> Hydra::new_exchange(
    AssetType asset_type_,
    std::string exchange_id_,
    std::string source_dir_,
    Frequency freq_,
    std::string dt_format_,
    std::optional<std::vector<std::string>> asset_ids,
    std::optional<std::shared_ptr<MarketAsset>> market_asset_,
    std::optional<std::string> holiday_file)
{
    // create the new exchange instance
    this->is_built = false;
    auto res = this->p->exchanges.new_exchange(asset_type_, exchange_id_, source_dir_, freq_, dt_format_);
    if (res.is_exception()) return AgisResult<bool>(res.get_exception());
    
    // load in holiday file if needed
    if (holiday_file.has_value()) {
        auto exchange = this->get_exchanges().get_exchange(exchange_id_);
        exchange.value()->load_trading_calendar(holiday_file.value());
    }
    
    // restore the exchange by loading in the asset data
    return this->p->exchanges.restore_exchange(exchange_id_, asset_ids, market_asset_);
}


//============================================================================
PortfolioPtr const Hydra::new_portfolio(std::string id, double cash)
{
    if (this->p->portfolios.__portfolio_exists(id))
    {
        AGIS_THROW("portfolio already exists");
    }
    this->is_built = false;
    auto portfolio = std::make_unique<Portfolio>(this->p->router, id, cash);
    portfolio->__set_exchange_map(&this->p->exchanges);
    this->p->portfolios.__register_portfolio(std::move(portfolio));

    return this->get_portfolio(id);
}


//============================================================================
std::expected<bool, AgisException> Hydra::register_broker(BrokerPtr broker)
{
    auto broker_opt = this->p->brokers.get_broker(broker->get_id());
    if (broker_opt.has_value()) {
        return std::unexpected<AgisException>("Broker with id " + broker->get_id() + " already exists");
    }
    else {
		this->p->brokers.register_broker(std::move(broker));
		return true;
	}
}


//============================================================================
BrokerMap* Hydra::__get_brokers() const noexcept
{
    return &this->p->brokers;
}


//============================================================================
AgisRouter* Hydra::__get_router() noexcept
{
    return &this->p->router;
}


//============================================================================
ExchangeMap const& Hydra::get_exchanges() const noexcept
{
    return this->p->exchanges; 
}


//============================================================================
ExchangeMap& Hydra::__get_exchanges() noexcept
{
    return this->p->exchanges;
}


//============================================================================
std::expected<ExchangePtr, AgisStatusCode>
Hydra::get_exchange(std::string const& exchange_id) const
{
    auto res = this->p->exchanges.get_exchange(exchange_id);
    if (res.has_value()) return res.value();
    return std::unexpected<AgisStatusCode>(AgisStatusCode::INVALID_ARGUMENT);
}


//============================================================================
PortfolioMap const& Hydra::get_portfolios() const noexcept
{
    return this->p->portfolios;
}


//============================================================================
void Hydra::register_strategy(std::unique_ptr<AgisStrategy> strategy)
{
    // Only benchmark strategies can have spaces in their names
    auto strategy_id = strategy->get_strategy_id();
    auto strategy_type = strategy->get_strategy_type();
    if (strategy_type != AgisStrategyType::BENCHMARK &&
        !is_valid_class_name(strategy_id))
	{
		AGIS_THROW("Strategy ID must not contain spaces");
	}

    if (this->strategies.__strategy_exists(strategy_id))
    {
        AGIS_THROW("strategy already exsits");
    }

#ifdef USE_LUAJIT
    if (strategy->get_strategy_type() == AgisStrategyType::LUAJIT) {
        // init lua state if needed
        if(!this->lua) this->lua = new sol::state();
        init_lua_interface(this->lua);
        auto lua_strategy = static_cast<AgisLuaStrategy*>(strategy.get());
        lua_strategy->set_lua_ptr(this->lua);
    }
#endif

    // set strategy exchange map pointer
    strategy->__set_exchange_map(&this->p->exchanges);

    // register the strategy to the strategy map
    this->strategies.register_strategy(std::move(strategy));
    this->p->portfolios.__reload_strategies(&this->strategies);     // because of pointer invalidation    
    this->is_built = false;
}


//============================================================================
AGIS_API PortfolioPtr const Hydra::get_portfolio(std::string const& portfolio_id) const
{
    return this->p->portfolios.__get_portfolio(portfolio_id);
}


//============================================================================
std::expected<BrokerPtr, AgisException> Hydra::get_broker(std::string const& broker_id)
{
    return this->p->brokers.get_broker(broker_id);
}


//============================================================================
std::expected<BrokerPtr, AgisException> Hydra::new_broker(std::string const& broker_id)
{
    return this->p->brokers.new_broker(&this->p->router, broker_id);
}


//============================================================================
AgisStrategy const* Hydra::get_strategy(std::string strategy_id) const
{
    return this->strategies.get_strategy(strategy_id);
}


//============================================================================s
AgisStrategy* Hydra::__get_strategy(std::string strategy_id) const
{
    return this->strategies.__get_strategy(strategy_id);
}


//============================================================================
PortfolioMap& Hydra::__get_portfolios()
{
    return this->p->portfolios;
}


//============================================================================
NexusStatusCode Hydra::remove_exchange(std::string exchange_id_)
{
    this->is_built = false;
    return this->p->exchanges.remove_exchange(exchange_id_);
}


//============================================================================
NexusStatusCode Hydra::remove_portfolio(std::string portfolio_id_)
{
    if (!this->p->portfolios.__portfolio_exists(portfolio_id_)) {
        return NexusStatusCode::InvalidArgument;
    }
    this->p->portfolios.__remove_portfolio(portfolio_id_);
    return NexusStatusCode::Ok;
}


//============================================================================
void Hydra::remove_strategy(std::string const& strategy_id)
{
    auto index = this->strategies.__get_strategy_index(strategy_id);
    this->strategies.__remove_strategy(strategy_id);
    this->p->portfolios.__remove_strategy(index);
}


//============================================================================
std::vector<std::string> Hydra::get_asset_ids(std::string exchange_id_) const
{
    return this->p->exchanges.get_asset_ids(exchange_id_);
}


//============================================================================
AgisResult<AssetPtr> Hydra::get_asset(std::string const& asset_id) const
{
    return this->p->exchanges.get_asset(asset_id);
}


//============================================================================
AgisResult<std::string> Hydra::asset_index_to_id(size_t const& index) const
{
    return this->p->exchanges.get_asset_id(index);
}


//============================================================================
AgisResult<std::string> Hydra::strategy_index_to_id(size_t const& index) const
{
    return this->strategies.__get_strategy_id(index);
}


//============================================================================
AgisResult<std::string> Hydra::portfolio_index_to_id(size_t const& index) const
{
    return this->p->portfolios.__get_portfolio_id(index);
}


//============================================================================
size_t Hydra::get_candle_count() const noexcept
{
    return this->p->exchanges.get_candle_count(); 
}


//============================================================================
std::span<long long>  Hydra::__get_dt_index(bool cutoff) const noexcept
{
    return this->p->exchanges.__get_dt_index(cutoff);
}


//============================================================================
bool Hydra::asset_exists(std::string asset_id) const
{
    return this->p->exchanges.asset_exists(asset_id);
}


//============================================================================
bool Hydra::portfolio_exists(std::string const& portfolio_id) const
{
    return this->p->portfolios.__portfolio_exists(portfolio_id);
}


//============================================================================
bool Hydra::strategy_exists(std::string const& strategy_id) const
{
    return this->strategies.__strategy_exists(strategy_id);
}


//============================================================================
std::expected<bool, AgisException>
Hydra::restore_exchanges(rapidjson::Document const& j)
{
    try{
        this->p->exchanges.restore(j);
    }
	catch (std::exception& e)
	{
		return std::unexpected<AgisException>(AGIS_EXCEP(e.what()));
	}
    return true;
}

//============================================================================
void Hydra::clear()
{
    this->strategies.__clear();
    this->p->exchanges.__clear();
    this->p->portfolios.__clear();
}


//============================================================================
std::expected<bool, AgisException> Hydra::build()
{
    size_t n = this->p->exchanges.__get_dt_index(false).size();
    auto res = this->p->exchanges.__build();
    if (!res.has_value()) return res;
    this->p->portfolios.__build(n);

    // register the strategies to the portfolio after they have all been added to prevent
    // references from being invalidated when a new strategy is added
    auto& strats = this->strategies.__get_strategies_mut();
    for (auto& strat : strats)
    {
        strat.second->__build(&this->p->router, &this->p->brokers);
        auto strat_ref = std::ref(strat.second);
        this->p->portfolios.__register_strategy(strat_ref);
    }
    auto res_s = this->strategies.build();
    if (res_s.is_exception()) return std::unexpected<AgisException>(res_s.get_exception());
    this->p->exchanges.__clean_up();
    return true;
}


//============================================================================
void Hydra::__reset()
{
    this->current_index = 0;
    this->p->exchanges.__reset();
    this->p->portfolios.__reset();
    this->p->router.__reset();
    AGIS_TRY(this->strategies.__reset();)

    Order::__reset_counter();
    Trade::__reset_counter();
}


//============================================================================
std::expected<bool, AgisException> Hydra::__cleanup()
{
    auto& strategies = this->strategies.__get_strategies_mut();
    std::vector<std::string> disabled_strategies;
    for (auto& [index, strategy] : strategies)
    {
        if (strategy->__is_disabled())
        {
            auto id = this->strategies.__get_strategy_id(index).unwrap();
            disabled_strategies.push_back(id);
            strategy->__set_is_disabled(false);
        }
    }
    // get an error message listing all the disabled strategies
    if (disabled_strategies.size() > 0)
	{
		std::string error_msg = "The following strategies were disabled: ";
		for (auto& id : disabled_strategies)
		{
			error_msg += id + ", ";
		}
		error_msg.pop_back();
		error_msg.pop_back();
		return std::unexpected<AgisException>(AGIS_EXCEP(error_msg));
	}
    return true;
}


//============================================================================
std::expected<rapidjson::Value, AgisException>
Hydra::save_state(rapidjson::Document::AllocatorType& allocator)
{
    rapidjson::Value j(kObjectType);

    // Save exchanges
    j.AddMember("exchanges", this->p->exchanges.to_json(), allocator);

    // Save covariance matrix
    auto cov_matrix_res = this->p->exchanges.get_covariance_matrix();
    if (!cov_matrix_res.is_exception()) {
        auto cov_matrix = cov_matrix_res.unwrap();
        if (cov_matrix->get_lookback() != 0) {
            j.AddMember("covariance_lookback", cov_matrix->get_lookback(), allocator);
            j.AddMember("covariance_step", cov_matrix->get_step_size(), allocator);
        }
    }

    // Save portfolios
    AGIS_ASSIGN_OR_RETURN(portfolio_json, this->p->portfolios.to_json(allocator));
    j.AddMember("portfolios", portfolio_json.Move(), allocator);
    return j;
}


//============================================================================
std::expected<AgisStrategyPtr,AgisException> 
strategy_from_json(
    PortfolioPtr const & portfolio,
    BrokerPtr broker,
    const Value& strategy_json)
{
    AgisStrategyType strategy_type = StringToAgisStrategyType(strategy_json["strategy_type"].GetString());
    std::string strategy_id = strategy_json["strategy_id"].GetString();
    std::string trading_window = strategy_json["trading_window"].GetString();

    bool beta_scale = strategy_json["beta_scale"].GetBool();
    bool beta_hedge = strategy_json["beta_hedge"].GetBool();
    bool beta_trace = strategy_json["beta_trace"].GetBool();
    bool net_leverage_trace = strategy_json["net_leverage_trace"].GetBool();
    bool vol_trace = strategy_json["vol_trace"].GetBool();

    bool is_live = strategy_json["is_live"].GetBool();
    double allocation = strategy_json["allocation"].GetDouble();

    std::optional<double> max_leverage = std::nullopt;
    std::optional<size_t> step_frequency = std::nullopt;
    if (strategy_json.HasMember("max_leverage")) {
        max_leverage = strategy_json["max_leverage"].GetDouble();
    }
    if (strategy_json.HasMember("step_frequency")) {
        step_frequency = strategy_json["step_frequency"].GetUint64();
    }

    AgisStrategyPtr strategy = nullptr;
    // create new strategy pointer based on the strategy type
    if (strategy_type == AgisStrategyType::FLOW)
    {
        strategy = std::make_unique<AbstractAgisStrategy>(
            portfolio,
            broker,
            strategy_id,
            allocation
        );
    }
    else if (strategy_type == AgisStrategyType::BENCHMARK)
    {
        strategy = std::make_unique<BenchMarkStrategy>(
            portfolio,
            broker,
            strategy_id
        );
    }
    else if (strategy_type == AgisStrategyType::LUAJIT) {
        if (!strategy_json.HasMember("lua_script_path")) {
            return std::unexpected<AgisException>(AGIS_EXCEP("LUAJIT strategy missing script path"));
        }
        std::string script_path = strategy_json["lua_script_path"].GetString();
        try {
            strategy = std::make_unique<AgisLuaStrategy>(
                portfolio,
                broker,
                strategy_id,
                allocation,
                fs::path(script_path),
                true
            );
        }
        catch (std::exception& e) {
            return std::unexpected<AgisException>(AGIS_EXCEP(e.what()));
		}
    }
    else return std::unexpected<AgisException>(AGIS_EXCEP("Invalid strategy type"));

    // set stored flags on the strategy
    try {
        strategy->set_trading_window(trading_window).unwrap();
        strategy->set_beta_scale_positions(beta_scale, false).unwrap();
        strategy->set_beta_hedge_positions(beta_hedge, false).unwrap();
        strategy->set_beta_trace(beta_trace, false).unwrap();
        strategy->set_net_leverage_trace(net_leverage_trace).unwrap();
        strategy->set_vol_trace(vol_trace).unwrap();
        strategy->set_max_leverage(max_leverage);
        strategy->set_step_frequency(step_frequency);
    }
    catch (std::exception& e) {
        return std::unexpected<AgisException>(AGIS_EXCEP(e.what()));
    }

    return std::move(strategy);
}


//============================================================================
std::expected<bool, AgisException>
Hydra::restore_portfolios(Document const& j)
{
    // if portfolio does not exist return true
    if (!j.HasMember("hydra_state") || !j["hydra_state"].HasMember("portfolios")) {
        return true;
    }
    
    AGIS_ASSIGN_OR_RETURN(res, this->p->portfolios.restore(this->p->router, j));
    
    // restore the exchange map pointer for the portfolio as well as the strateges
    const Value& portfolios = j["hydra_state"]["portfolios"];
    for (Value::ConstMemberIterator portfolio_json = portfolios.MemberBegin(); portfolio_json != portfolios.MemberEnd(); ++portfolio_json) {
        std::string portfolio_id = portfolio_json->name.GetString();
        const Value& portfolio_value = portfolio_json->value;

        // set the exchange map pointer
        auto& portfolio = this->get_portfolio(portfolio_id);
        portfolio->__set_exchange_map(&this->p->exchanges);

        // attempt to register the strategies to the portfolio
        JSON_GET_OR_CONTINUE(strategies, portfolio_value, "strategies");
        for (Value::ConstValueIterator strategy_json = strategies.Begin(); strategy_json != strategies.End(); ++strategy_json) {
            const AgisStrategyType strategy_type = StringToAgisStrategyType(strategy_json->FindMember("strategy_type")->value.GetString());
            if (strategy_type == AgisStrategyType::CPP) {
                continue;
            }
            AGIS_ASSIGN_OR_RETURN(broker, this->p->brokers.get_broker(portfolio_value["broker_id"].GetString()));
            AGIS_ASSIGN_OR_RETURN(strategy, strategy_from_json(portfolio, broker, *strategy_json));
            this->register_strategy(std::move(strategy));
        }
    }
    return true;
}


//============================================================================
AGIS_API AgisResult<bool> Hydra::init_covariance_matrix(size_t lookback, size_t step_size)
{
    auto res = this->p->exchanges.init_covariance_matrix(lookback, step_size);
    if (res.is_exception()) return res;
    this->is_built = false;
    return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> Hydra::set_market_asset(
    std::string const& exchange_id,
    std::string const& asset_id,
    bool disable,
    std::optional<size_t> beta_lookback
) 
{
    this->is_built = false;
    return this->p->exchanges.set_market_asset(exchange_id, asset_id, disable, beta_lookback);
}


//============================================================================
AGIS_API ThreadSafeVector<SharedOrderPtr> const&
Hydra::get_order_history()
{
    return this->p->router.get_order_history(); 
}


template struct AGIS_API AgisResult<bool>;
template struct AGIS_API AgisResult<std::string>;
template struct AGIS_API AgisResult<size_t>;
template struct AGIS_API AgisResult<TradeExitPtr>;
template struct AGIS_API AgisResult<AssetObserverPtr>;

template class AGIS_API AgisMatrix<double>;
template class AGIS_API StridedPointer<double>;