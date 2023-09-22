#pragma once
#include "pch.h" 

#include "Utils.h"
#include "Hydra.h"
#include "Exchange.h"
#include "AgisObservers.h"

//============================================================================
Hydra::Hydra(int logging_, bool init_lua_state):
    router(
        this->exchanges,
        &this->portfolios)
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
}


//============================================================================
void Hydra::__step()
{
    // step assets and exchanges forward in time
    this->exchanges.step();
    auto& expired_index_list = this->exchanges.__get_expired_index_list();
    
    // evaluate the portfolios at the current prices
    this->portfolios.__evaluate(true, true);

    // process strategy logic at end of each time step
    bool step;
    AGIS_TRY(step = this->strategies.__next());
    if (step) { this->router.__process(); };

    // process orders on the exchange and route to their portfolios on fill
    this->exchanges.__process_orders(this->router, true);
    this->router.__process();

    // evaluate the portfolios on close, then remove any position for assets that are expiring.
    AGIS_TRY(this->portfolios.__evaluate(true));
    this->portfolios.__on_assets_expired(this->router, expired_index_list);
    this->router.__process();
}



//============================================================================
AGIS_API AgisResult<bool> Hydra::__run()
{
    if (!this->is_built) 
    {
        AGIS_DO_OR_RETURN(this->build(), bool);
        this->is_built = true; 
    }
    
    try {
        this->__reset();
    }
    catch (std::exception& e) {
		return AgisResult<bool>(AGIS_EXCEP(e.what()));
	}
    
    auto index = this->exchanges.__get_dt_index(false);
    for (size_t i = this->current_index; i < index.size(); i++)
    {
        AGIS_TRY_RESULT(this->__step(), bool);
        this->current_index++;
    }
    return this->__cleanup();
}


//============================================================================
AgisResult<bool> Hydra::new_exchange(
    std::string exchange_id_,
    std::string source_dir_,
    Frequency freq_,
    std::string dt_format_,
    std::optional<std::vector<std::string>> asset_ids,
    std::optional<MarketAsset> market_asset_)
{
    this->is_built = false;
    auto res = this->exchanges.new_exchange(exchange_id_, source_dir_, freq_, dt_format_);
    if (res.is_exception()) return AgisResult<bool>(res.get_exception());
    return this->exchanges.restore_exchange(exchange_id_, asset_ids, market_asset_);
}


//============================================================================
AGIS_API PortfolioPtr const Hydra::new_portfolio(std::string id, double cash)
{
    if (this->portfolios.__portfolio_exists(id))
    {
        AGIS_THROW("portfolio already exists");
    }
    this->is_built = false;
    auto portfolio = std::make_unique<Portfolio>(this->router, id, cash);
    this->portfolios.__register_portfolio(std::move(portfolio));

    return this->get_portfolio(id);
}


//============================================================================
AGIS_API void Hydra::register_strategy(std::unique_ptr<AgisStrategy> strategy)
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
        auto lua_strategy = dynamic_cast<AgisLuaStrategy*>(strategy.get());
        lua_strategy->set_lua_ptr(this->lua);
    }
#endif

    // set strategy exchange map pointer
    strategy->__set_exchange_map(&this->exchanges);

    // register the strategy to the strategy map
    this->strategies.register_strategy(std::move(strategy));
    this->portfolios.__reload_strategies(&this->strategies);     // because of pointer invalidation    
    this->is_built = false;
}


//============================================================================
AGIS_API PortfolioPtr const Hydra::get_portfolio(std::string const& portfolio_id) const
{
    return this->portfolios.__get_portfolio(portfolio_id);
}


//============================================================================
AGIS_API AgisStrategy const* Hydra::get_strategy(std::string strategy_id) const
{
    return this->strategies.get_strategy(strategy_id);
}


//============================================================================s
AGIS_API AgisStrategy* Hydra::__get_strategy(std::string strategy_id) const
{
    return this->strategies.__get_strategy(strategy_id);
}


//============================================================================
AGIS_API NexusStatusCode Hydra::remove_exchange(std::string exchange_id_)
{
    this->is_built = false;
    return this->exchanges.remove_exchange(exchange_id_);
}


//============================================================================
AGIS_API NexusStatusCode Hydra::remove_portfolio(std::string portfolio_id_)
{
    if (!this->portfolios.__portfolio_exists(portfolio_id_)) {
        return NexusStatusCode::InvalidArgument;
    }
    this->portfolios.__remove_portfolio(portfolio_id_);
    return NexusStatusCode::Ok;
}


//============================================================================
AGIS_API void Hydra::remove_strategy(std::string const& strategy_id)
{
    auto index = this->strategies.__get_strategy_index(strategy_id);
    this->strategies.__remove_strategy(strategy_id);
    this->portfolios.__remove_strategy(index);
}


//============================================================================
std::vector<std::string> Hydra::get_asset_ids(std::string exchange_id_) const
{
    return this->exchanges.get_asset_ids(exchange_id_);
}


//============================================================================
AgisResult<AssetPtr> Hydra::get_asset(std::string const& asset_id) const
{
    return this->exchanges.get_asset(asset_id);
}


//============================================================================
AGIS_API AgisResult<std::string> Hydra::asset_index_to_id(size_t const& index) const
{
    return this->exchanges.get_asset_id(index);
}


//============================================================================
AGIS_API AgisResult<std::string> Hydra::strategy_index_to_id(size_t const& index) const
{
    return this->strategies.__get_strategy_id(index);
}

//============================================================================
AGIS_API AgisResult<std::string> Hydra::portfolio_index_to_id(size_t const& index) const
{
    return this->portfolios.__get_portfolio_id(index);
}


//============================================================================
bool Hydra::asset_exists(std::string asset_id) const
{
    return this->exchanges.asset_exists(asset_id);
}


//============================================================================
AGIS_API bool Hydra::portfolio_exists(std::string const& portfolio_id) const
{
    return this->portfolios.__portfolio_exists(portfolio_id);
}


//============================================================================
AGIS_API bool Hydra::strategy_exists(std::string const& strategy_id) const
{
    return this->strategies.__strategy_exists(strategy_id);
}


AGIS_API AgisResult<bool> Hydra::restore_exchanges(json const& j)
{
    try{
        this->exchanges.restore(j);
    }
	catch (std::exception& e)
	{
		return AgisResult<bool>(AGIS_EXCEP(e.what()));
	}
    return AgisResult<bool>(true);
}

//============================================================================
void Hydra::clear()
{
    this->strategies.__clear();
    this->exchanges.__clear();
    this->portfolios.__clear();
}


//============================================================================
AGIS_API AgisResult<bool> Hydra::build()
{
    size_t n = this->exchanges.__get_dt_index(false).size();
    this->exchanges.__build();
    this->portfolios.__build(n);

    // register the strategies to the portfolio after they have all been added to prevent
    // references from being invalidated when a new strategy is added
    auto& strats = this->strategies.__get_strategies_mut();
    for (auto& strat : strats)
    {
        strat.second->__build(&this->router);
        auto strat_ref = std::ref(strat.second);
        this->portfolios.__register_strategy(strat_ref);
    }
    AGIS_DO_OR_RETURN(this->strategies.build(), bool);
    this->exchanges.__clean_up();
    return AgisResult<bool>(true);
}


//============================================================================
void Hydra::__reset()
{
    this->current_index = 0;
    this->exchanges.__reset();
    this->portfolios.__reset();
    this->router.__reset();
    AGIS_TRY(this->strategies.__reset();)

    Order::__reset_counter();
    Trade::__reset_counter();
}


//============================================================================
AgisResult<bool> Hydra::__cleanup()
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
		return AgisResult<bool>(AGIS_EXCEP(error_msg));
	}
    return AgisResult<bool>(true);
}


//============================================================================
void Hydra::save_state(json& j)
{
    // save exchanges
    j["exchanges"] = this->exchanges.to_json();
    auto cov_matrix = this->exchanges.get_covariance_matrix();
    if (!cov_matrix.is_exception()) {
        j["covariance_lookback"] = cov_matrix.unwrap()->get_lookback();
        j["covariance_step"] = cov_matrix.unwrap()->get_step_size();
    }

    // save portfolios
    j["portfolios"] = this->portfolios.to_json();
}


AgisResult<AgisStrategyPtr> strategy_from_json(
    PortfolioPtr const & portfolio,
    json const& strategy_json)
{
    AgisStrategyType strategy_type = strategy_json["strategy_type"];
    std::string strategy_id = strategy_json.at("strategy_id");
    std::string trading_window = strategy_json.at("trading_window");

    bool beta_scale = strategy_json.value("beta_scale", false);
    bool beta_hedge = strategy_json.value("beta_hedge", false);
    bool beta_trace = strategy_json.value("beta_trace", false);
    bool net_leverage_trace = strategy_json.value("net_leverage_trace", false);
    bool vol_trace = strategy_json.value("vol_trace", false);

    bool is_live = strategy_json.at("is_live");
    double allocation = strategy_json.at("allocation");

    std::optional<double> max_leverage = std::nullopt;
    std::optional<size_t> step_frequency = std::nullopt;
    if (strategy_json.contains("max_leverage")) {
        max_leverage = strategy_json.at("max_leverage");
    }
    if (strategy_json.contains("step_frequency")) {
        step_frequency = strategy_json.at("step_frequency");
    }

    AgisStrategyPtr strategy = nullptr;
    // create new strategy pointer based on the strategy type
    if (strategy_type == AgisStrategyType::FLOW)
    {
        strategy = std::make_unique<AbstractAgisStrategy>(
            portfolio,
            strategy_id,
            allocation
        );
    }
    else if (strategy_type == AgisStrategyType::BENCHMARK)
    {
        strategy = std::make_unique<BenchMarkStrategy>(
            portfolio,
            strategy_id
        );
    }
    else if (strategy_type == AgisStrategyType::LUAJIT) {
        if (!strategy_json.contains("lua_script_path")) {
            return AgisResult<AgisStrategyPtr>(AGIS_EXCEP("LUAJIT strategy missing script path"));
        }
        std::string script_path = strategy_json.at("lua_script_path");
        try {
            strategy = std::make_unique<AgisLuaStrategy>(
                portfolio,
                strategy_id,
                allocation,
                fs::path(script_path),
                true
            );
        }
        catch (std::exception& e) {
			return AgisResult<AgisStrategyPtr>(AGIS_EXCEP(e.what()));
		}
    }
    else return AgisResult<AgisStrategyPtr>(AGIS_EXCEP("Invalid strategy type"));

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
        return AgisResult<AgisStrategyPtr>(AGIS_EXCEP(e.what()));
    }

    return AgisResult<AgisStrategyPtr>(std::move(strategy));
}


//============================================================================
AgisResult<bool> Hydra::restore_portfolios(json const& j)
{
    this->portfolios.restore(this->router, j);


    // build the abstract strategies stored in the json
    json portfolios = j["portfolios"];
    for (const auto& portfolio_json : portfolios.items())
    {
        std::string portfolio_id = portfolio_json.key();
        json& j = portfolio_json.value();
        json& strategies = j["strategies"];
        auto& portfolio = this->get_portfolio(portfolio_id);
        for (const auto& strategy_json : strategies)
        {
            AgisStrategyType strategy_type = strategy_json["strategy_type"];
            if (strategy_type == AgisStrategyType::CPP) continue;

            auto strategy = strategy_from_json(portfolio, strategy_json);
            if(strategy.is_exception()) return AgisResult<bool>(strategy.get_exception());
            this->register_strategy(std::move(strategy.unwrap()));
        }
    }
    return AgisResult<bool>(true);
}


//============================================================================
AGIS_API AgisResult<bool> Hydra::init_covariance_matrix(size_t lookback, size_t step_size)
{
    auto res = this->exchanges.init_covariance_matrix(lookback, step_size);
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
    return this->exchanges.set_market_asset(exchange_id, asset_id, disable, beta_lookback);
}


template struct AGIS_API AgisResult<bool>;
template struct AGIS_API AgisResult<json>;
template struct AGIS_API AgisResult<std::string>;
template struct AGIS_API AgisResult<size_t>;
template struct AGIS_API AgisResult<TradeExitPtr>;
template struct AGIS_API AgisResult<AssetObserverPtr>;