#include "pch.h"

#include <tbb/parallel_for_each.h>

#include "AgisStrategy.h"


//============================================================================
const std::function<double(double, double)> agis_init = [](double a, double b) { return b; };
const std::function<double(double, double)> agis_identity = [](double a, double b) { return a; };
const std::function<double(double, double)> agis_add = [](double a, double b) { return a + b; };
const std::function<double(double, double)> agis_subtract = [](double a, double b) { return a - b; };
const std::function<double(double, double)> agis_multiply = [](double a, double b) { return a * b; };
const std::function<double(double, double)> agis_divide = [](double a, double b) { return a / b; };



//============================================================================
std::unordered_map<std::string, AgisOperation> agis_function_map = {
   {"INIT", agis_init},
   {"IDENTITY", agis_identity},
   {"ADD", agis_add},
   {"SUBTRACT", agis_subtract},
   {"MULTIPLY", agis_multiply},
   {"DIVIDE", agis_divide}
};


//============================================================================
std::unordered_map<std::string, ExchangeQueryType> agis_query_map = {
   {"Default", ExchangeQueryType::Default},
   {"NLargest", ExchangeQueryType::NLargest},
   {"NSmallest", ExchangeQueryType::NSmallest },
   {"NExtreme", ExchangeQueryType::NExtreme},
};


//============================================================================
std::vector<std::string>  agis_query_strings =
{
	"Default",	/// return all assets in view
	"NLargest",	/// return the N largest
	"NSmallest",/// return the N smallest
	"NExtreme"	/// return the N/2 smallest and largest
};


//============================================================================
std::vector<std::string> agis_function_strings = {
	"INIT",
	"IDENTITY",
	"ADD",
	"SUBTRACT",
	"MULTIPLY",
	"DIVIDE"
};


//============================================================================
std::vector<std::string> agis_strat_alloc_strings = {
	//"UNITS",
	//"DOLLARS",
	"PCT"
};


//============================================================================
std::vector<std::string> agis_trading_windows = {
	"",
	"US_EQUITY_REG_HRS"
};


//============================================================================
std::unordered_map<std::string, TradingWindow> agis_trading_window_map = {
	{"US_EQUITY_REG_HRS", us_equity_reg_hrs}
};


TradingWindow us_equity_reg_hrs  = {
	{9,30},
	{16,0}
};

//============================================================================
TradingWindow all_hrs  = {
	{0,0},
	{23,59}
};


//============================================================================
const std::function<double(
	const std::shared_ptr<Asset>& asset,
	const std::string& col,
	int offset
	)> asset_feature_lambda = [](
		const std::shared_ptr<Asset>& asset,
		const std::string& col,
		int offset
		) { 
	return asset->get_asset_feature(col, offset); 
};


//============================================================================
const std::function<double(
	const std::shared_ptr<Asset>&,
	const std::vector<
	std::pair<Operation, std::function<double(const std::shared_ptr<Asset>&)>>
	>& operations)> asset_feature_lambda_chain = [](
		const std::shared_ptr<Asset>& asset,
		const std::vector<std::pair<Operation, std::function<double(const std::shared_ptr<Asset>&)>>>& operations
		)
{
	double result = 0;
	for (const auto& operation : operations) {
		const auto& op = operation.first;
		const auto& assetFeatureLambda = operation.second;
		result = op(result, assetFeatureLambda(asset));
	}
	return result;
};


//============================================================================
std::unordered_map<std::string, AllocType> agis_strat_alloc_map = {
   {"UNITS", AllocType::UNITS},
   {"DOLLARS", AllocType::DOLLARS},
   {"PCT", AllocType::PCT},
};

//============================================================================
void AgisStrategy::__reset()
{
	this->order_history.clear();
	this->cash_history.clear();
	this->nlv_history.clear();
	this->cash = this->starting_cash;
	this->nlv = this->cash;
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
		auto& window = *this->trading_window;
		auto& current_tp = this->exchange_map->get_tp();
		if (current_tp < window.first || current_tp > window.second) { return false; }
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
		for (auto asset_index : results)
		{
			auto trade_opt = this->get_trade(asset_index);
			auto units = trade_opt.value().get()->units;
			this->place_market_order(
				asset_index,
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

			double offset = abs(size) / abs(exsisting_units);
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
	
	std::for_each(
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

void AgisStrategyMap::__clear()
{
	this->strategies.clear();
	this->strategy_id_map.clear();
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
void AbstractAgisStrategy::next()
{
	auto& ev_lambda_ref = *this->ev_lambda_struct;
	
	//TODO maybe make warmup set to hydra index not exchange index
	if (ev_lambda_ref.exchange->__get_exchange_index() < ev_lambda_ref.warmup) { return; }

	auto ev = ev_lambda_ref.exchange_view_labmda(
		ev_lambda_ref.asset_lambda,
		ev_lambda_ref.exchange,
		ev_lambda_ref.query_type,
		ev_lambda_ref.N
	);
	auto& strat_alloc_ref = *ev_lambda_ref.strat_alloc_struct;
	switch (this->ev_opp_type)
		{
		case ExchangeViewOpp::UNIFORM: {
			ev.uniform_weights(strat_alloc_ref.target_leverage);
			break;
		}
		case ExchangeViewOpp::LINEAR_INCREASE: {
			ev.linear_increasing_weights(strat_alloc_ref.target_leverage);
			break;
		}
		case ExchangeViewOpp::LINEAR_DECREASE: {
			ev.linear_decreasing_weights(strat_alloc_ref.target_leverage);
			break;
		}
	}
	this->strategy_allocate(
		&ev,
		strat_alloc_ref.epsilon,
		strat_alloc_ref.clear_missing,
		std::nullopt,
		strat_alloc_ref.alloc_type
	);
}


//============================================================================
void AbstractAgisStrategy::build()
{
	if (!ev_lambda_struct.has_value()) {
		throw std::runtime_error("missing abstract lambda strategy");
	}

	ExchangePtr exchange = ev_lambda_struct.value().exchange;
	this->exchange_subscribe(exchange->get_exchange_id());
}

void AbstractAgisStrategy::extract_ev_lambda()
{
	this->ev_lambda_struct = this->ev_lambda();
	auto& ev_lambda_ref = *this->ev_lambda_struct;

	// set ev alloc type
	auto& strat_alloc_ref = *ev_lambda_ref.strat_alloc_struct;
	if (strat_alloc_ref.ev_opp_type == "UNIFORM")
		this->ev_opp_type = ExchangeViewOpp::UNIFORM;
	else if (strat_alloc_ref.ev_opp_type == "LINEAR_DECREASE")
		this->ev_opp_type = ExchangeViewOpp::LINEAR_DECREASE;
	else if (strat_alloc_ref.ev_opp_type == "LINEAR_INCREASE")
		this->ev_opp_type = ExchangeViewOpp::LINEAR_INCREASE;
}

//============================================================================
void AbstractAgisStrategy::restore(fs::path path)
{
}

//============================================================================
void AbstractAgisStrategy::to_json(json& j)
{
	AgisStrategy::to_json(j);
}
