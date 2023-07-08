#pragma once
#include "pch.h" 

#include "Hydra.h"
#include "Exchange.h"

#define NUM_THREADS 2


Hydra::Hydra(int logging_):
    router(
        this->exchanges,
        this->portfolios)
{
    this->logging = logging_;
}

void Hydra::__step()
{
    // step assets and exchanges forward in time
    this->exchanges.step();

    // begin listening for orders
    
    // process orders on the open exchange and route to their portfolios on fill
    this->exchanges.__process_orders(this->router, false);
    this->router.__process();
    
    // process strategy logic at end of each time step
    this->strategies.__next();
    this->router.__process();

    // process orders on the exchange and route to their portfolios on fill
    this->exchanges.__process_orders(this->router, true);
    this->router.__process();
}

//============================================================================
NexusStatusCode Hydra::new_exchange(
    std::string exchange_id_,
    std::string source_dir_,
    Frequency freq_,
    std::string dt_format_)
{
    return this->exchanges.new_exchange(exchange_id_, source_dir_, freq_, dt_format_);
}


//============================================================================
AGIS_API void Hydra::new_portfolio(std::string id, double cash)
{
    auto portfolio = std::make_unique<Portfolio>(id, cash);
    this->portfolios.__register_portfolio(std::move(portfolio));
}


//============================================================================
AGIS_API void Hydra::register_strategy(std::unique_ptr<AgisStrategy> strategy)
{
    strategy->__build(&this->router, &this->exchanges);
    this->strategies.register_strategy(std::move(strategy));
}


//============================================================================
AGIS_API PortfolioPtr const& Hydra::get_portfolio(std::string const& portfolio_id)
{
    return this->portfolios.__get_portfolio(portfolio_id);
}

//============================================================================
NexusStatusCode Hydra::remove_exchange(std::string exchange_id_)
{
    return this->exchanges.remove_exchange(exchange_id_);
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
void Hydra::clear()
{
    this->exchanges.__clear();
    this->portfolios.__clear();
}


//============================================================================
AGIS_API void Hydra::build()
{
    this->exchanges.build();
}


//============================================================================
void Hydra::save_state(json& j)
{
    j["exchanges"] = this->exchanges.to_json();
}


//============================================================================
void Hydra::restore(json const& j)
{
    this->exchanges.restore(j);
}