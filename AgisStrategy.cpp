#include "pch.h"

#include <tbb/parallel_for_each.h>

#include "AgisStrategy.h"
#include "Exchange.h"




//============================================================================
void AgisStrategy::__reset()
{
	this->order_history.clear();
	this->cash_history.clear();
	this->nlv_history.clear();
	this->reset();
}

//============================================================================
void AgisStrategy::__build(
	AgisRouter* router_,
	ExchangeMap* exchange_map
)
{
	this->router = router_;
	this->exchange_map = exchange_map;
	this->cash = this->portfolio_allocation * this->portfolio->get_cash();
	this->nlv = this->cash;
	this->subscribe();
}


//============================================================================
void AgisStrategy::__evaluate(bool on_close)
{
	if (on_close)
	{
		this->nlv_history.push_back(this->nlv);
		this->cash_history.push_back(this->cash);
	}
}


//============================================================================
void AgisStrategy::to_json(json& j)
{
	j["strategy_id"] = this->strategy_id;
	j["allocation"] = this->portfolio_allocation;
}

//============================================================================
void AgisStrategy::exchange_subscribe(std::string const& exchange_id)
{
	auto exchange = this->exchange_map->get_exchange(exchange_id);

	this->exchange_subsrciption = exchange_id;
	this->is_subsribed = true;
	this->__exchange_step = &exchange->__took_step;
}


//============================================================================
bool AgisStrategy::__is_step()
{
	if (!(*this->__exchange_step)) { return false; }
	if (this->trading_window.has_value())
	{
		auto window = this->trading_window.value();
		auto current_time = this->exchange_map->get_datetime();
		if (current_time < window.first || current_time > window.second) { return false; }
	}
	return true;
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
AGIS_API ExchangePtr const AgisStrategy::get_exchange(std::string const& id) const
{
	return this->exchange_map->get_exchange(id);
}


//============================================================================
AGIS_API void AgisStrategy::strategy_allocate(
	ExchangeView const* exchange_view,
	double epsilon,
	bool clear_missing,
	std::optional<TradeExitPtr> exit,
	AllocType alloc_type)
{
	auto position_ids = this->portfolio->get_strategy_positions(this->strategy_index);
	auto& allocation = exchange_view->view;

	// if clear_missing, clear and trades with asset index not in the allocation
	if (clear_missing)
	{
		std::vector<size_t> results;
		for (const auto& element : position_ids) {
			auto it = std::find_if(allocation.begin(), allocation.end(),
				[&element](const auto& pair) { return pair.first == element; });
			if (it == allocation.end()) {
				results.push_back(element);
			}
			else {
				continue;
			}
		}
		for (auto asset_id : results)
		{
			auto trade_opt = this->get_trade(asset_id);
			auto units = trade_opt.value().get()->units;
			this->place_market_order(
				asset_id,
				-1 * units
			);
		}
	}
	// generate orders based on the allocation passed
	for (auto& alloc : allocation)
	{
		size_t asset_index = alloc.first;
		double size = alloc.second;
		auto trade_opt = this->get_trade(asset_index);

		switch (alloc_type)
		{
			case AllocType::UNITS:
				break;
			case AllocType::DOLLARS: {
				auto market_price = this->exchange_map->__get_market_price(asset_index, true);
				size /= market_price;
				break; 
			}
			case AllocType::PCT: {
				auto market_price = this->exchange_map->__get_market_price(asset_index, true);
				size = gmp_mult(size, this->nlv) / market_price;
				break;
			}
		}

		// if trade already exists, reflect those units on the allocation size
		if (trade_opt.has_value())
		{
			double exsisting_units = trade_opt.value().get()->units;
			gmp_sub_assign(size, exsisting_units);

			double offset = abs((exsisting_units - size) / exsisting_units);
			if (offset < epsilon) { continue; }
		}

		// minimum required units in order to place an order
		if (abs(size) < 1e-10) { continue; }

		if (exit.has_value())
		{
			auto x_ptr = exit.value()->clone();
			this->place_market_order(asset_index, size, std::move(x_ptr));
		}
		else
		{
			this->place_market_order(
				asset_index,
				size
			);
		}
	}
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
bool AgisStrategyMap::__next()
{
	// Define a lambda function that calls next for each strategy
	std::atomic<bool> flag(false);
	
	auto strategy_next = [&](auto& strategy) {
		if (!strategy.second->__is_step()) { return; }
		strategy.second->next();
		flag.store(true, std::memory_order_relaxed);
	};
	
	tbb::parallel_for_each(
		this->strategies.begin(),
		this->strategies.end(),
		strategy_next
	);
	return flag.load(std::memory_order_relaxed);
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


//============================================================================
void AgisStrategyMap::__build()
{
	for (auto& strategy : this->strategies)
	{
		strategy.second->build();
	}
}


//============================================================================
AGIS_API void agis_realloc(ExchangeView* allocation, double c)
{
	for (auto& pair : allocation->view) {
		pair.second = c;
	}
}


//============================================================================
void AbstractAgisStrategy::restore(json& j)
{
}

//============================================================================
void AbstractAgisStrategy::to_json(json& j)
{
	AgisStrategy::to_json(j);
}
