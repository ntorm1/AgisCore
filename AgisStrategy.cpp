#include "pch.h"

#include <tbb/parallel_for_each.h>

#include "AgisStrategy.h"

std::atomic<size_t> AgisStrategy::strategy_counter(0);




void AgisStrategy::__build(
	AgisRouter* router_,
	PortfolioMap* portfolo_map,
	ExchangeMap* exchange_map
)
{
	this->router = router_;
	this->portfolo_map = portfolo_map;
	this->exchange_map = exchange_map;
}

void AgisStrategy::place_market_order(
	size_t asset_index_,
	double units_,
	size_t portfilio_index)
{
	this->router->place_order(std::make_unique<MarketOrder>(
		asset_index_,
		units_,
		this->strategy_index,
		portfilio_index
	));
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

