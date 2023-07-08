#include "pch.h"

#include <tbb/parallel_for_each.h>

#include "AgisStrategy.h"

void AgisStrategy::__build(
	AgisRouter* router_,
	ExchangeMap* exchange_map
)
{
	this->router = router_;
	this->exchange_map = exchange_map;
}

void AgisStrategy::place_market_order(
	size_t asset_index_,
	double units_)
{
	this->router->place_order(std::make_unique<MarketOrder>(
		asset_index_,
		units_,
		this->strategy_index,
		this->get_portfolio_index()
	));
}

void AgisStrategy::place_market_order(std::string asset_id, double units)
{
	auto asset_index = this->exchange_map->get_asset_index(asset_id);
	return this->place_market_order(asset_index, units);
}


void AgisStrategyMap::register_strategy(std::unique_ptr<AgisStrategy> strategy)
{
	this->strategies.emplace(
		strategy->get_strategy_index(),
		std::move(strategy)
	);
}

void AgisStrategyMap::__next()
{
	// Define a lambda function that calls next for each strategy
	auto strategy_next = [&](auto& strategy) {
		strategy.second->next();
	};

	tbb::parallel_for_each(
		this->strategies.begin(),
		this->strategies.end(),
		strategy_next
	);
}

