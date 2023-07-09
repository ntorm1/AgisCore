#include "pch.h"

#include <tbb/parallel_for_each.h>

#include "AgisStrategy.h"


void AgisStrategy::__reset()
{
	this->order_history.clear();
}

//============================================================================
void AgisStrategy::__build(
	AgisRouter* router_,
	ExchangeMap* exchange_map
)
{
	this->router = router_;
	this->exchange_map = exchange_map;
}


//============================================================================
std::optional<TradeRef> AgisStrategy::get_trade(size_t asset_index)
{
	auto position = this->portfolio->get_position(asset_index);
	if (!position.has_value()) { return std::nullopt; }
	return position->get()->__get_trade(this->strategy_index);
}


//============================================================================
std::optional<TradeRef> AgisStrategy::get_trade(std::string const& asset_id)
{
	auto asset_index = this->exchange_map->get_asset_index(asset_id);
	auto position = this->portfolio->get_position(asset_index);
	if (!position.has_value()) { return std::nullopt; }
	return position->get()->__get_trade(this->strategy_index);
}


//============================================================================
void AgisStrategy::place_market_order(
	size_t asset_index_,
	double units_,
	std::optional<TradeExitPtr> exit)
{
	this->router->place_order(std::make_unique<MarketOrder>(
		asset_index_,
		units_,
		this->strategy_index,
		this->get_portfolio_index(),
		std::move(exit)
	));
}


//============================================================================
void AgisStrategy::place_market_order(
	std::string const & asset_id, 
	double units,
	std::optional<TradeExitPtr> exit
	)
{
	auto asset_index = this->exchange_map->get_asset_index(asset_id);
	this->place_market_order(asset_index, units, std::move(exit));
}



//============================================================================
void AgisStrategyMap::register_strategy(AgisStrategyPtr strategy)
{
	this->strategy_id_map.emplace(
		strategy->get_strategy_id(),
		strategy->get_strategy_index()
	);
	this->strategies.emplace(
		strategy->get_strategy_index(),
		std::move(strategy)
	);
}

const AgisStrategyRef AgisStrategyMap::get_strategy(std::string strategy_id)
{
	auto strategy_index = this->strategy_id_map.at(strategy_id);
	return std::ref(this->strategies.at(strategy_index));
}


//============================================================================
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

//============================================================================
void AgisStrategyMap::__reset()
{
	auto strategy_reset = [&](auto& strategy) {
		strategy.second->__reset();
	};

	tbb::parallel_for_each(
		this->strategies.begin(),
		this->strategies.end(),
		strategy_reset
	);
}

