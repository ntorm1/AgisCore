#pragma once
#include "pch.h" 

#include "Utils.h"
#include "Hydra.h"
#include "Exchange.h"

constexpr auto NUM_THREADS = 2;


//============================================================================
Hydra::Hydra(int logging_):
    router(
        this->exchanges,
        &this->portfolios)
{
    this->logging = logging_;
}


//============================================================================
void Hydra::__step()
{
    // step assets and exchanges forward in time
    this->exchanges.step();
    auto& expired_index_list = this->exchanges.__get_expired_index_list();
    
    // process strategy logic at end of each time step
    bool step = this->strategies.__next();
    if (step) { this->router.__process(); };

    // process orders on the exchange and route to their portfolios on fill
    this->exchanges.__process_orders(this->router, true);
    this->router.__process();

    // evaluate the portfolios on close, then remove any position for assets that are expiring.
    this->portfolios.__evaluate(this->router, this->exchanges, true);
    this->portfolios.__on_assets_expired(this->router, expired_index_list);
    this->router.__process();
}



//============================================================================
AGIS_API void Hydra::__run()
{
    if (!this->is_built) { this->build(); this->is_built = true; }
    this->__reset();

    size_t step_count = this->exchanges.__get_dt_index().size();
    for (size_t i = 0; i < step_count; i++)
    {
        this->__step();
    }
}


//============================================================================
NexusStatusCode Hydra::new_exchange(
    std::string exchange_id_,
    std::string source_dir_,
    Frequency freq_,
    std::string dt_format_)
{
    this->is_built = false;
    return this->exchanges.new_exchange(exchange_id_, source_dir_, freq_, dt_format_);
}


//============================================================================
AGIS_API PortfolioPtr const& Hydra::new_portfolio(std::string id, double cash)
{
    if (this->portfolios.__portfolio_exists(id))
    {
        AGIS_THROW("portfolio already exists");
    }
    this->is_built = false;
    auto portfolio = std::make_unique<Portfolio>(id, cash);
    this->portfolios.__register_portfolio(std::move(portfolio));

    return this->get_portfolio(id);
}


//============================================================================
AGIS_API void Hydra::register_strategy(std::unique_ptr<AgisStrategy> strategy)
{
    if (this->strategies.__strategy_exists(strategy->get_strategy_id()))
    {
        AGIS_THROW("strategy already exsits");
    }
    if (!is_valid_class_name(strategy->get_strategy_id()))
    {
        AGIS_THROW("Strategy ID must contain characters that are letters, digits, or underscores");
    }

    // build the strategy instance
    strategy->__build(&this->router, &this->exchanges);

    // register the strategy to the strategy map
    std::string id = strategy->get_strategy_id();
    this->strategies.register_strategy(std::move(strategy));

    // register the strategy to the portfolio
    auto strat_ref = std::ref(
        this->strategies.get_strategy(id)
    );
    this->portfolios.__register_strategy(strat_ref);
    this->is_built = false;
}


//============================================================================
AGIS_API PortfolioPtr const& Hydra::get_portfolio(std::string const& portfolio_id)
{
    return this->portfolios.__get_portfolio(portfolio_id);
}


//============================================================================
AGIS_API const AgisStrategyRef Hydra::get_strategy(std::string strategy_id)
{
    return this->strategies.get_strategy(strategy_id);
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
std::vector<std::string> Hydra::get_asset_ids(std::string exchange_id_)
{
    return this->exchanges.get_asset_ids(exchange_id_);
}


//============================================================================
std::optional<std::shared_ptr<Asset> const> Hydra::get_asset(std::string const& asset_id) const
{
    return this->exchanges.get_asset(asset_id);
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


//============================================================================
void Hydra::clear()
{
    this->strategies.__clear();
    this->exchanges.__clear();
    this->portfolios.__clear();
}


//============================================================================
AGIS_API void Hydra::build()
{
    this->exchanges.__build();
    this->strategies.__build();
}


//============================================================================
AGIS_API void Hydra::__reset()
{
    this->exchanges.__reset();
    this->portfolios.__reset();
    this->router.__reset();
    this->strategies.__reset();
}


//============================================================================
void Hydra::save_state(json& j)
{
    j["exchanges"] = this->exchanges.to_json();
    j["portfolios"] = this->portfolios.to_json();
}


//============================================================================
void Hydra::restore(json const& j)
{
    this->exchanges.restore(j);
    this->portfolios.restore(j);


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
            std::string strategy_id = strategy_json["strategy_id"];
            std::string trading_window = strategy_json["trading_window"];

            double allocation = strategy_json["allocation"];
            auto strategy = std::make_unique<AbstractAgisStrategy>(
                portfolio,
                strategy_id,
                allocation
            );

            strategy->set_trading_window(trading_window).unwrap();

            this->register_strategy(std::move(strategy));
        }
    }
}