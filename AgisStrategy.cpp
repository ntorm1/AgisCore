#include "AgisStrategy.h"
#include "pch.h"
#include <fstream>

#include <tbb/parallel_for_each.h>

#include "AgisAnalysis.h"
#include "AgisErrors.h"
#include "AgisStrategy.h"

std::atomic<size_t> AgisStrategy::strategy_counter(0);



//============================================================================
void AgisStrategy::__reset()
{
	this->trades.clear();
	this->order_history.clear();
	
	this->cash_history.clear();
	this->nlv_history.clear();
	this->beta_history.clear();
	this->net_leverage_ratio_history.clear();


	this->cash = this->starting_cash;
	this->nlv = this->cash;
	this->net_beta.has_value() ? this->net_beta = 0.0f : std::nullopt;
	this->net_leverage_ratio.has_value() ? this->net_leverage_ratio = 0.0f : std::nullopt;

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

	auto n = exchange_map->__get_dt_index().size();
	this->nlv_history.reserve(n);
	this->cash_history.reserve(n);
	if (this->net_beta.has_value()) this->beta_history.reserve(n);
	if (this->net_leverage_ratio.has_value()) this->net_leverage_ratio_history.reserve(n);
}

//============================================================================
void AgisStrategy::__evaluate(bool on_close)
{
	if (on_close)
	{
		this->nlv_history.push_back(this->nlv);
		this->cash_history.push_back(this->cash);
		if (this->net_beta.has_value()) this->beta_history.push_back(this->net_beta.value());
		if (this->net_leverage_ratio.has_value()) {
			if (this->nlv - this->cash < 1e-7) this->net_leverage_ratio = 1.0f;
			else this->net_leverage_ratio = (this->nlv - this->cash) / this->nlv;
			if (this->net_leverage_ratio.value() > 1.25f) {
				auto y = 2;
			}
			this->net_leverage_ratio_history.push_back(this->net_leverage_ratio.value());
		}

	}
}


//============================================================================s
void AgisStrategy::__zero_out_tracers()
{
	this->nlv = this->cash;
	if (this->net_beta.has_value()) this->net_beta = 0.0f;
	if (this->net_leverage_ratio.has_value()) this->net_leverage_ratio = 0.0f;
	if (this->limits.max_leverage.has_value()) this->limits.phantom_cash = 0.0f;
}


//============================================================================
void AgisStrategy::to_json(json& j) const
{
	j["is_live"] = this->is_live;
	j["strategy_id"] = this->strategy_id;
	j["strategy_type"] = this->strategy_type;
	j["allocation"] = this->portfolio_allocation;
	j["trading_window"] = trading_window_to_key_str(this->trading_window);
	j["beta_scale"] = this->apply_beta_scale;
	j["beta_hedge"] = this->apply_beta_hedge;
	j["beta_trace"] = this->net_beta.has_value();
	j["net_leverage_trace"] = this->net_leverage_ratio.has_value();

	if (this->limits.max_leverage.has_value()) {
		j["max_leverage"] = this->limits.max_leverage.value();
	}
	if (this->step_frequency.has_value()) {
		j["step_frequency"] = this->step_frequency.value();
	}
}


//============================================================================
AgisResult<bool> AgisStrategy::exchange_subscribe(std::string const& exchange_id)
{
	if (!this->exchange_map->exchange_exists(exchange_id))
	{
		return AgisResult<bool>(AGIS_EXCEP("Invalid exchange id: " + exchange_id));
	}
	auto exchange = this->exchange_map->get_exchange(exchange_id);

	this->exchange_subsrciption = exchange_id;
	this->__exchange_step = &exchange->__took_step;
	return AgisResult<bool>(true);
}


//============================================================================
bool AgisStrategy::__is_step()
{
	// allow for manual disable of a strategy
	if (!this->is_live) { return false; }

	// check to see if the strategy has subsribed to an exchange and if the exchange took step
	if (!this->__exchange_step || !(*this->__exchange_step)) { return false; }

	// check to see if the step frequency is set and if the current step is a multiple of the frequency
	if (this->step_frequency.has_value()) {
		if (this->exchange_map->__get_current_index() % this->step_frequency.value() != 0) { return false; }
	}

	// check to see if strategy is within it's trading window
	if (this->trading_window.has_value())
	{
		auto& window = *this->trading_window;
		auto& current_tp = this->exchange_map->get_tp();
		if (current_tp < window.first || current_tp > window.second) { return false; }
	}
	return true;
}


//============================================================================
std::optional<SharedTradePtr> AgisStrategy::get_trade(size_t asset_index)
{
	if (!this->trades.contains(asset_index)) { return std::nullopt; }
	return this->trades.at(asset_index);
}


//============================================================================
void AgisStrategy::__validate_order(OrderPtr& order)
{
	// test if the order will cause the portfolio leverage to exceede it's limit
	double cash_estimate = 0.0f;
	if (this->limits.max_leverage.has_value()) {
		auto x = this->exchange_map->__get_market_price(order->get_asset_index(), true);
		cash_estimate = order->get_units() * x;
		x = (this->nlv - (this->cash - this->limits.phantom_cash - cash_estimate)) / this->nlv;
		if (x > this->limits.max_leverage.value())
		{
			order->__set_state(OrderState::REJECTED);
			return;
		}
	}
	// test to see if shorting is allowed for the strategy
	if (!this->limits.allow_shorting && order->get_units() < 0)
	{
		order->__set_state(OrderState::REJECTED);
		return;
	}

	// order is valid, reflect the estimated cost in the phantom cash balance
	if (this->limits.max_leverage.has_value()) {
		this->limits.phantom_cash += cash_estimate;
	}
}


//============================================================================
std::optional<SharedTradePtr> AgisStrategy::get_trade(std::string const& asset_id)
{
	auto asset_index = this->exchange_map->get_asset_index(asset_id);
	if (!this->trades.count(asset_index)) { return std::nullopt; }
	return this->trades.at(asset_index);
}


//============================================================================
AGIS_API ExchangePtr const AgisStrategy::get_exchange(std::string const& id) const
{
	return this->exchange_map->get_exchange(id);
}


//============================================================================
AGIS_API void AgisStrategy::strategy_allocate(
	ExchangeView& exchange_view,
	double epsilon,
	bool clear_missing,
	std::optional<TradeExitPtr> exit,	
	AllocType alloc_type)
{
	if (this->apply_beta_scale) AGIS_DO_OR_THROW(exchange_view.beta_scale());
	if (this->apply_beta_hedge) AGIS_DO_OR_THROW(exchange_view.beta_hedge(this->target_leverage));

	auto& allocation = exchange_view.view;

	// generate orders based on the allocation passed
	for (auto& alloc : allocation)
	{
		size_t asset_index = alloc.first;
		double size = alloc.second;
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
				size *=  (this->nlv / market_price);
				break;
			}
		}

		// if trade already exists, reflect those units on the allocation size
		auto trade_opt = this->get_trade(asset_index);
		if (trade_opt.has_value())
		{
			SharedTradePtr& trade = *trade_opt;
			trade->strategy_alloc_touch = true;
			double exsisting_units = trade->units;
			size -= exsisting_units;

			double offset = abs(size) / abs(exsisting_units);
			if(epsilon > 0.0 && offset < epsilon) { continue; }
			// if epsilon is less than 0, only place new order if the new order is reversing the 
			// direction of the trade
			else if(epsilon < 0.0) {
				if (size * exsisting_units > 0) continue;
				if (size * exsisting_units < 0 && abs(size) < abs(exsisting_units)) continue;
			}
		}

		// minimum required units in order to place an order
		if (abs(size) < 1e-10) { continue; }

		// make deep copy of the exit if needed
		std::optional<TradeExitPtr> exit_copy = std::nullopt;
		if (exit.has_value()) {
			auto exit_raw_ptr = exit.value()->clone();
			exit_copy = std::make_optional<TradeExitPtr>(exit_raw_ptr);
		}
		this->place_market_order(asset_index,size, exit_copy);
	}

	// if clear_missing, clear and trades with asset index not in the allocation
	if (clear_missing && this->trades.size())
	{
		for (const auto& element : this->trades) {
			if (!element.second->strategy_alloc_touch)
			{
				this->place_market_order(
					element.first,
					-1 * element.second->units
				);
			}
			element.second->strategy_alloc_touch = false;
		}
	}
}


//============================================================================
AgisResult<bool> AgisStrategy::set_trading_window(std::string const& window_name)
{
	if (window_name == "")
	{
		return AgisResult<bool>(true);
	}
	if (!agis_trading_window_map.contains(window_name)) {
		return AgisResult<bool>(AGIS_EXCEP("Invalid trading window: " + window_name));
	}
	TradingWindow window = agis_trading_window_map.at(window_name);
	this->set_trading_window(window);
	return  AgisResult<bool>(true);
}


//============================================================================
void AGIS_API AgisStrategy::place_order(OrderPtr order)
{
	if(is_order_validating) this->__validate_order(order);
	this->router->place_order(std::move(order));
}


//============================================================================
void AgisStrategy::place_market_order(
	size_t asset_index_,
	double units_,
	std::optional<TradeExitPtr> exit)
{
	this->place_order(std::make_unique<Order>(
		OrderType::MARKET_ORDER,
		asset_index_,
		units_,
		this->strategy_index,
		this->get_portfolio_index(),
		exit,
		this->strategy_type == AgisStrategyType::BENCHMARK
	));
}


//============================================================================
void AGIS_API AgisStrategy::place_limit_order(size_t asset_index_, double units_, double limit, std::optional<TradeExitPtr> exit)
{
	auto order = std::make_unique<Order>(
		OrderType::LIMIT_ORDER,
		asset_index_,
		units_,
		this->strategy_index,
		this->get_portfolio_index(),
		std::move(exit)
	);
	order->phantom_order = this->strategy_type == AgisStrategyType::BENCHMARK;
	order->set_limit(limit);
	this->place_order(std::move(order));
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
AgisStrategyMap::~AgisStrategyMap()
{
	// if strategy was created by the AgisStrategy.dll, release ownership.
	// idk if correct but without this get read access violation when calling destructor
	// of strategies created by AgisStrategy.dll
	for (auto& [id, strategy] : this->strategies)
	{
		if (strategy->get_strategy_type() == AgisStrategyType::CPP) {
			AgisStrategyPtr strategy = std::move(this->strategies.at(id));
			AgisStrategy* raw_ptr = strategy.release();
		}
	}
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


//============================================================================
AgisStrategy const* AgisStrategyMap::get_strategy(std::string const& strategy_id) const
{
	auto strategy_index = this->strategy_id_map.at(strategy_id);
	return this->strategies.at(strategy_index).get();
}


//============================================================================
AgisStrategy* AgisStrategyMap::__get_strategy(std::string const& strategy_id) const
{
	auto strategy_index = this->strategy_id_map.at(strategy_id);
	return this->strategies.at(strategy_index).get();
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

	std::for_each(
		this->strategies.begin(),
		this->strategies.end(),
		strategy_reset
	);
}


//============================================================================
void AgisStrategyMap::__clear()
{
	this->strategies.clear();
	this->strategy_id_map.clear();
}


//============================================================================
AgisResult<bool> AgisStrategyMap::__build()
{
	for (auto& strategy : this->strategies)
	{
		if (!strategy.second->__is_live()) continue;
		try {
			strategy.second->build();
		}
		catch (const std::exception& ex) {
			return AgisResult<bool>(AgisException(AGIS_EXCEP(ex.what())));
		}
	}
	return AgisResult<bool>(true);
}


//============================================================================
void AgisStrategyMap::__remove_strategy(std::string const& id)
{
	auto index = this->strategy_id_map.at(id);
	this->strategies.erase(index);
	this->strategy_id_map.erase(id);
}


//============================================================================
AgisResult<std::string> AgisStrategyMap::__get_strategy_id(size_t index) const
{
	// find the strategy with the given index and return it's id
	auto strategy = std::find_if(
		this->strategies.begin(),
		this->strategies.end(),
		[index](auto& strategy) {
			return strategy.second->get_strategy_index() == index;
		}
	);
	if (strategy == this->strategies.end())
	{
		return AgisResult<std::string>(AGIS_EXCEP("failed to find strategy"));
	}
	// return the strategy id
	return AgisResult<std::string>(strategy->second->get_strategy_id());
}


//============================================================================
std::vector<std::string> AgisStrategyMap::__get_strategy_ids() const
{
	// return every key in the strategy_id_map
	std::vector<std::string> ids;
	std::transform(
		this->strategy_id_map.begin(),
		this->strategy_id_map.end(),
		std::back_inserter(ids),
		[](auto& strategy) {
			return strategy.first;
		}
	);
	return ids;
}


//============================================================================
std::string opp_to_str(const AgisOperation& func)
{
	int a = 1; int b = 2;
	if (func(a,b) == agis_init(a,b)) {
		return "agis_init";
	}
	else if (func(a, b) == agis_identity(a, b)) {
		return "agis_identity";
	}
	else if (func(a, b) == agis_add(a, b)) {
		return "agis_add";
	}
	else if (func(a, b) == agis_subtract(a, b)) {
		return "agis_subtract";
	}
	else if (func(a, b) == agis_multiply(a, b)) {
		return "agis_multiply";
	}
	else if (func(a, b) == agis_divide(a, b)) {
		return "agis_divide";
	}
	else {
		return "Unknown function";
	}
}

bool is_numeric(const std::string& str) {
	for (char c : str) {
		if (!std::isdigit(c)) {
			return false;
		}
	}
	return true;
}


//============================================================================
AgisResult<TradeExitPtr> parse_trade_exit(
	TradeExitType trade_exit_type, 
	const std::string& trade_exit_params)
{
	try {
		switch (trade_exit_type)
		{
		case TradeExitType::BARS: {
			if (!is_numeric(trade_exit_params))
			{
				return AgisResult<TradeExitPtr>(AGIS_EXCEP("invalid exit bars"));
			}
			size_t result = std::stoull(trade_exit_params);
			return AgisResult<TradeExitPtr>(std::make_shared<ExitBars>(result));
		}
		case TradeExitType::THRESHOLD: {
			// expects string line [-.05,.1] (5% stop loss, 10% take profit)
			// Remove leading '[' and trailing ']'
			std::string trimmedStr = trade_exit_params.substr(1, trade_exit_params.size() - 2);
			// Split the string by ','
			std::istringstream ss(trimmedStr);
			std::string lowerStr, upperStr;
			std::getline(ss, lowerStr, ',');
			std::getline(ss, upperStr, ',');
			auto stop_loss = std::stod(lowerStr);
			auto take_profit = std::stod(upperStr);
			return AgisResult<TradeExitPtr>(std::make_shared<ExitThreshold>(stop_loss, take_profit));
		}
		default:
			return AgisResult<TradeExitPtr>(AGIS_EXCEP("invalid trade exit type"));
		}
	}
	catch (const std::exception& ex) {
		return AgisResult<TradeExitPtr>(AgisException(AGIS_EXCEP(ex.what())));
	}
}


//============================================================================
void AbstractAgisStrategy::next()
{
	auto& ev_lambda_ref = *this->ev_lambda_struct;
	
	// verify strategy warmup period has passed
	if (ev_lambda_ref.exchange->__get_exchange_index() < ev_lambda_ref.warmup) { return; }

	ExchangeView ev;
	AGIS_TRY(
		ev = ev_lambda_ref.exchange_view_labmda(
			ev_lambda_ref.asset_lambda,
			ev_lambda_ref.exchange,
			ev_lambda_ref.query_type,
			ev_lambda_ref.N
		);
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
		case ExchangeViewOpp::CONDITIONAL_SPLIT: {
			ev.conditional_split(strat_alloc_ref.target_leverage, this->ev_opp_param.value());
			break;
		}
		case ExchangeViewOpp::UNIFORM_SPLIT: {
			ev.uniform_split(strat_alloc_ref.target_leverage);
			break;
		}
		case ExchangeViewOpp::CONSTANT: {
			auto c = this->ev_opp_param.value() * strat_alloc_ref.target_leverage;
			ev.constant_weights(c);
			break;
		}
		default: {
			throw std::runtime_error("invalid exchange view operation");
		}
	}

	this->strategy_allocate(
		ev,
		strat_alloc_ref.epsilon,
		strat_alloc_ref.clear_missing,
		strat_alloc_ref.trade_exit,
		strat_alloc_ref.alloc_type
	);
}


//============================================================================
void AbstractAgisStrategy::build()
{
	if (!ev_lambda_struct.has_value()) {
		throw std::runtime_error(this->get_strategy_id() + " missing abstract lambda strategy");
	}

	ExchangePtr exchange = ev_lambda_struct.value().exchange;
	auto res = this->exchange_subscribe(exchange->get_exchange_id());

	// validate exchange subscription
	if (res.is_exception()) throw res.get_exception();

	// validate beta hedge
	if (this->apply_beta_hedge || this->apply_beta_scale)
	{
		auto market_asset = exchange->__get_market_asset();
		if(market_asset.is_exception()) throw market_asset.get_exception();
	}

	// set the strategy warmup period
	this->warmup = this->ev_lambda_struct.value().warmup;

	// set the strategy target leverage
	this->target_leverage = this->ev_lambda_struct.value().strat_alloc_struct.value().target_leverage;
}


//============================================================================
AgisResult<bool> AbstractAgisStrategy::extract_ev_lambda()
{
	this->ev_lambda_struct = this->ev_lambda();

	if (!this->ev_lambda_struct.has_value()) {
		return AgisResult<bool>(AGIS_EXCEP("missing ev lambda struct"));
	}

	auto& ev_lambda_ref = *this->ev_lambda_struct;

	if (!ev_lambda_ref.exchange) {
		return AgisResult<bool>(AGIS_EXCEP("missing exchange"));
	}

	// set ev alloc type
	auto& strat_alloc_ref = *ev_lambda_ref.strat_alloc_struct;
	if (strat_alloc_ref.ev_opp_type == "UNIFORM")
		this->ev_opp_type = ExchangeViewOpp::UNIFORM;
	else if (strat_alloc_ref.ev_opp_type == "LINEAR_DECREASE")
		this->ev_opp_type = ExchangeViewOpp::LINEAR_DECREASE;
	else if (strat_alloc_ref.ev_opp_type == "LINEAR_INCREASE")
		this->ev_opp_type = ExchangeViewOpp::LINEAR_INCREASE;
	else if (strat_alloc_ref.ev_opp_type == "CONDITIONAL_SPLIT")
		this->ev_opp_type = ExchangeViewOpp::CONDITIONAL_SPLIT;
	else if (strat_alloc_ref.ev_opp_type == "UNIFORM_SPLIT")
		this->ev_opp_type = ExchangeViewOpp::UNIFORM_SPLIT;
	else if (strat_alloc_ref.ev_opp_type == "CONSTANT")
		this->ev_opp_type = ExchangeViewOpp::CONSTANT;
	else AGIS_THROW("invalid exchange view opp type");

	if (this->ev_opp_type == ExchangeViewOpp::CONDITIONAL_SPLIT || 
		this->ev_opp_type == ExchangeViewOpp::CONSTANT)
	{
		std::optional<double> val = strat_alloc_ref.ev_extra_opp.value();
		if (!val.has_value())
		{
			return AgisResult<bool>(AGIS_EXCEP("exchange view opperation expected extra ev parameters"));
		}
		else
		{
			this->ev_opp_param = val;
		}
	}
	return AgisResult<bool>(true);
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


//============================================================================
void BenchMarkStrategy::build()
{
	// set the benchmark asset id and index
	AgisResult<AssetPtr const> asset_response = this->exchange_map->__get_market_asset(this->frequency);
	if (asset_response.is_exception()) AGIS_THROW(asset_response.get_exception());
	this->asset_id = asset_response.unwrap()->get_asset_id();
	this->asset_index = this->exchange_map->get_asset_index(this->asset_id);

	// subscrive to the benchmark asset's exchange
	auto& exchange_id = asset_response.unwrap()->get_exchange_id();
	this->exchange_subscribe(exchange_id);
}


//============================================================================
void BenchMarkStrategy::evluate()
{
	// evaluate the benchmark strategy at the close
	for (auto& trade_pair : this->trades)
	{
		auto& trade = trade_pair.second;
		auto last_price = trade->__asset->__get_market_price(true);
		trade->evaluate(last_price, true, false);
	}
	AgisStrategy::__evaluate(true);
}


//============================================================================
void BenchMarkStrategy::next()
{
	// if the first step then allocate all funds to the asset
	if (this->i == 1) return;
	ExchangeView ev;
	ev.view.emplace_back(this->asset_index, 1.0);
	this->strategy_allocate(
		ev,
		0.0,
		true,
		std::nullopt,
		AllocType::PCT
	);
	this->i++;
}


//============================================================================
void str_replace_all(std::string& source, const std::string& oldStr, const std::string& newStr) {
	size_t pos = source.find(oldStr);
	while (pos != std::string::npos) {
		source.replace(pos, oldStr.length(), newStr);
		pos = source.find(oldStr, pos + newStr.length());
	}
}


//============================================================================
void code_gen_write(fs::path filename, std::string const& source)
{
	// Check if the file already exists and has the same content as source
	bool file_exists = false;
	std::string existing_content;

	std::ifstream file_input(filename);
	if (file_input.is_open()) {
		file_exists = true;
		std::stringstream buffer;
		buffer << file_input.rdbuf();
		existing_content = buffer.str();
		file_input.close();
	}

	// Compare existing content with source
	if (file_exists && existing_content == source) {
		return;
	}

	// Write the new content to the file
	std::ofstream file_output(filename);
	if (file_output.is_open()) {
		file_output << source;
		file_output.close();
	}
	else {
		AGIS_THROW("Failed to open " + filename.string() + " for writing.");
	}
}


//============================================================================
std::string trading_window_to_str(std::optional<TradingWindow> window_op) {
	if (!window_op.has_value()) { return ""; }
	std::ostringstream codeStream;
	auto window = window_op.value();
	codeStream << "TradingWindow(";
	codeStream << "std::make_pair(TimePoint{" << window.first.hour << ", " << window.first.minute << "}, ";
	codeStream << "TimePoint{" << window.second.hour << ", " << window.second.minute << "})";
	codeStream << ")";

	return codeStream.str();
}


//============================================================================
void AbstractAgisStrategy::code_gen(fs::path strat_folder)
{
	if (!this->ev_lambda_struct.has_value())
	{
		AGIS_THROW("Abstract strategy has not been built yet");
	}
	auto exchange_id = this->ev_lambda_struct.value().exchange->get_exchange_id();
	auto portfolio_id = this->get_portfolio_id();
	auto warmup = this->ev_lambda_struct.value().warmup;
	auto& ev_lambda_ref = *this->ev_lambda_struct;
	auto& strat_alloc_ref = *ev_lambda_ref.strat_alloc_struct;

	std::string strategy_header = R"(#pragma once

#ifdef AGISSTRATEGY_EXPORTS // This should be defined when building the DLL
#  define AGIS_STRATEGY_API __declspec(dllexport)
#else
#  define AGIS_STRATEGY_API __declspec(dllimport)
#endif

// the following code is generated from an abstract strategy flow graph.
// EDIT IT AT YOUR OWN RISK 
#include "AgisStrategy.h"

class {STRATEGY_ID}_CPP : public AgisStrategy {
public:
	AGIS_STRATEGY_API {STRATEGY_ID}_CPP (
        PortfolioPtr const portfolio_
    ) : AgisStrategy("{STRATEGY_ID}_CPP", portfolio_, {ALLOC}) {
		this->strategy_type = AgisStrategyType::CPP;
		this->trading_window = {TRADING_WINDOW};
	};

    AGIS_STRATEGY_API inline static std::unique_ptr<AgisStrategy> create_instance(
        PortfolioPtr const& portfolio_
    ) 
	{
        return std::make_unique<{STRATEGY_ID}_CPP>(portfolio_);
    }

	AGIS_STRATEGY_API inline void reset() override {}

	AGIS_STRATEGY_API void build() override;

	AGIS_STRATEGY_API void next() override;

private:
	ExchangeViewOpp ev_opp_type = ExchangeViewOpp::{EV_OPP_TYPE};
	ExchangePtr exchange = nullptr;
	size_t warmup = {WARMUP};
};
)";

	// Replace the placeholder with the EV_OPP_TYPE value
	auto pos = strategy_header.find("{EV_OPP_TYPE}");
	strategy_header.replace(pos, 13, ev_opp_to_str(this->ev_opp_type));

	// Replace the allocation amount
	pos = strategy_header.find("{ALLOC}");
	strategy_header.replace(pos, 7, std::to_string(this->get_allocation()));

	// Replace the warmup 
	pos = strategy_header.find("{WARMUP}");
	strategy_header.replace(pos, 8, std::to_string(warmup));

	// Replace the trading_window
	pos = strategy_header.find("{TRADING_WINDOW}");
	if (this->trading_window.has_value()) {
		strategy_header.replace(pos, 16, trading_window_to_str(this->trading_window.value()));
	}
	else {
		strategy_header.replace(pos, 16, "std::nullopt");
	}

	// Replace strategy class name
	std::string place_holder = "{STRATEGY_ID}";
	std::string strategy_id = this->get_strategy_id();
	str_replace_all(strategy_header, place_holder, strategy_id);

	// Insert exchange ID
	std::string exchange_str = R"("{ID}")";
	pos = exchange_str.find("{ID}");
	exchange_str.replace(pos, 4, exchange_id);
	
	std::string build_method =
	R"(this->exchange = this->get_exchange()" + exchange_str + R"(); 
	this->exchange_subscribe(exchange->get_exchange_id());)";

	// build the vector of asset lambdas to be used when calling next
	std::string asset_lambda = R"(std::vector<AssetLambdaScruct> operations = { )";
	int i = 0;
	for (auto& pair : ev_lambda_ref.asset_lambda)
	{
		std::string lambda_mid;
		if (pair.is_operation()) {
			lambda_mid = R"(AssetLambdaScruct(AssetLambda({OPP}, [&](const AssetPtr& asset) {
			return asset->get_asset_feature("{COL}", {INDEX});
		}),{OPP}, "{COL}", {INDEX})
)";
			auto pos = lambda_mid.find("{OPP}");
			auto& asset_operation = pair.get_asset_operation_struct();
			lambda_mid.replace(pos, 5, opp_to_str(pair.get_agis_operation()));

			str_replace_all(lambda_mid, "{COL}", asset_operation.column);
			str_replace_all(lambda_mid, "{OPP}", opp_to_str(pair.get_agis_operation()));
			str_replace_all(lambda_mid, "{INDEX}", std::to_string(asset_operation.row));
		}
		else {
			lambda_mid = pair.get_asset_filter_struct().asset_filter_range.code_gen();
		}
		// append the asset lambda str rep to the vector containing the operations
		asset_lambda = asset_lambda + lambda_mid;
		if(i < ev_lambda_ref.asset_lambda.size() - 1)
		{
			asset_lambda = asset_lambda + ", ";
		}
		else {
			asset_lambda = asset_lambda + "};";
		}
		i++;
	}
	// agis strategy next method
	std::string next_method = R"(auto next_lambda = [&operationsRef](const AssetPtr& asset) -> AgisResult<double> {			
		return asset_feature_lambda_chain(
			asset, 
			operationsRef
		);
	};
		
	auto ev = this->exchange->get_exchange_view(
		next_lambda, 
		{EXCHANGE_QUERY_TYPE},
		{N}
	);

	{EV_TRANSFORM}

	this->strategy_allocate(
		ev,
		{EPSILON},
		{CLEAR},
		std::nullopt,
		AllocType::{ALLOC_TYPE}
	);

	)";

	// Replace the exchange query type
	pos = next_method.find("{EXCHANGE_QUERY_TYPE}");
	next_method.replace(pos, 21, ev_query_type(ev_lambda_ref.query_type));

	pos = next_method.find("{N}");
	next_method.replace(pos, 3, std::to_string(ev_lambda_ref.N));

	auto& strat_alloc_struct = *ev_lambda_ref.strat_alloc_struct;
	// Replace epsilon
	pos = next_method.find("{EPSILON}");
	next_method.replace(pos, 9, std::to_string(strat_alloc_struct.epsilon));

	// Clear position if missing
	pos = next_method.find("{CLEAR}");
	next_method.replace(pos, 7, std::to_string(strat_alloc_struct.clear_missing));

	// Strategy allocation type
	pos = next_method.find("{ALLOC_TYPE}");
	next_method.replace(pos, 12, alloc_to_str(strat_alloc_struct.alloc_type));

	// Replace ev transform
	auto target_leverage = std::to_string(strat_alloc_ref.target_leverage);
	std::string ev_opp_str;
	if (strat_alloc_ref.ev_opp_type == "UNIFORM")
		ev_opp_str = R"(ev.uniform_weights({LEV});)";
	else if (strat_alloc_ref.ev_opp_type == "LINEAR_DECREASE")
		ev_opp_str = R"(ev.linear_decreasing_weights({LEV});)";
	else if (strat_alloc_ref.ev_opp_type == "LINEAR_INCREASE")
		ev_opp_str = R"(ev.linear_increasing_weights({LEV});)";
	pos = ev_opp_str.find("{LEV}");
	ev_opp_str.replace(pos, 5, std::to_string(strat_alloc_struct.target_leverage));
	pos = next_method.find("{EV_TRANSFORM}");
	next_method.replace(pos, 14, ev_opp_str);

	std::string strategy_source = R"(
// the following code is generated from an abstract strategy flow graph.
// EDIT IT AT YOUR OWN RISK 

#include "{STRATEGY_ID}_CPP.h"
#include <functional> // for std::reference_wrapper

{LAMBDA_CHAIN}

void {STRATEGY_ID}_CPP::build(){
	// set the strategies target exchanges
	{BUILD_METHOD}
	
	this->set_beta_trace({BETA_TRACE});
	this->set_beta_scale_positions({BETA_SCALE});
	this->set_beta_hedge_positions({BETA_HEDGE});
	this->set_net_leverage_trace({NET_LEV});
};

void {STRATEGY_ID}_CPP::next(){
	if (this->exchange->__get_exchange_index() < this->warmup) { return; }

    auto& operationsRef = operations; // Create a reference to operations

	// define the lambda function the strategy will apply
	{NEXT_METHOD}
};
)";

	// Replace the placeholder with the BUILD_METHOD
	pos = strategy_source.find("{BUILD_METHOD}");
	strategy_source.replace(pos, 14, build_method);

	// Replace the placeholder with the NEXT_METHOD
	pos = strategy_source.find("{NEXT_METHOD}");
	strategy_source.replace(pos, 13, next_method);

	// Replace the lambda chain
	pos = strategy_source.find("{LAMBDA_CHAIN}");
	strategy_source.replace(pos, 14, asset_lambda);

	// Replace the lambda chain
	pos = strategy_source.find("{BETA_TRACE}");
	strategy_source.replace(pos, 12, (this->net_beta.has_value()) ? "true" : "false");

	// Replace the lambda chain
	pos = strategy_source.find("{BETA_SCALE}");
	strategy_source.replace(pos, 12, (apply_beta_scale) ? "true" : "false");

	// Replace the lambda chain
	pos = strategy_source.find("{BETA_HEDGE}");
	strategy_source.replace(pos, 12, (apply_beta_hedge) ? "true" : "false");

	// Replace the lambda chain
	pos = strategy_source.find("{NET_LEV}");
	strategy_source.replace(pos, 9, (this->net_leverage_ratio.has_value()) ? "true" : "false");

	// Replace strategy class name
	str_replace_all(strategy_source, place_holder, strategy_id);

	auto header_path = strat_folder / (strategy_id + "_CPP.h");
	auto source_path = strat_folder / (strategy_id + "_CPP.cpp");
	AGIS_TRY(code_gen_write(header_path, strategy_header))
	AGIS_TRY(code_gen_write(source_path, strategy_source))
}

AgisResult<bool> AbstractAgisStrategy::validate_market_asset()
{
	if (!this->ev_lambda_struct.has_value()) {
		return AgisResult<bool>(this->get_strategy_id() + " missing abstract lambda strategy");
	}

	ExchangePtr exchange = ev_lambda_struct.value().exchange;
	auto market_asset = exchange->__get_market_asset();
	if (market_asset.is_exception()) return AgisResult<bool>(market_asset.get_exception());
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> AgisStrategy::set_beta_trace(bool val, bool check)
{
	val ? this->net_beta = 0 : this->net_beta = std::nullopt;
	return AgisResult<bool>(true);
}

//============================================================================
AgisResult<bool> AgisStrategy::set_net_leverage_trace(bool val)
{
	val ? this->net_leverage_ratio = 0 : this->net_leverage_ratio = std::nullopt;
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> AbstractAgisStrategy::set_beta_scale_positions(bool val, bool check)
{
	if (!val) return AgisStrategy::set_beta_scale_positions(val, check);
	if(check) AGIS_DO_OR_RETURN(this->validate_market_asset(), bool);
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> AbstractAgisStrategy::set_beta_hedge_positions(bool val, bool check)
{
	if (!val) return AgisStrategy::set_beta_hedge_positions(val, check);
	if(check) AGIS_DO_OR_RETURN(this->validate_market_asset(), bool);
	this->apply_beta_hedge = val;
	return AgisResult<bool>(true);
}


//============================================================================
AGIS_API AgisResult<bool> AbstractAgisStrategy::set_beta_trace(bool val, bool check)
{
	if (!val) return AgisStrategy::set_beta_trace(val, check);
	if (check) AGIS_DO_OR_RETURN(this->validate_market_asset(), bool);
	return AgisStrategy::set_beta_trace(val);
}