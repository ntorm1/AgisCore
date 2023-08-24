#include "pch.h"
#include <fstream>
#include <sstream>
#include <tbb/parallel_for_each.h>


#include "AgisStrategy.h"
#include "AgisErrors.h"


std::atomic<size_t> AgisStrategy::strategy_counter(0);

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


//============================================================================
std::string trading_window_to_key_str(std::optional<TradingWindow> input_window_opt) {
	if (!input_window_opt.has_value()) { return ""; }
	auto input_window = input_window_opt.value();
	for (const auto& entry : agis_trading_window_map) {
		const TradingWindow& window = entry.second;
		if (window.second == input_window.second)
		{
			return entry.first;
		}
	}
	return ""; // Return an empty string if no match is found
}


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
const std::function<AgisResult<double>(
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
	const std::vector<AssetLambdaScruct>& operations)> asset_feature_lambda_chain = [](
		const std::shared_ptr<Asset>& asset,
		const std::vector<AssetLambdaScruct>& operations
		)
{
	double result = 0;
	for (const auto& operation : operations) {
		auto& lambda = operation.l;
		const auto& op = lambda.first;
		const auto& assetFeatureLambda = lambda.second;
		result = op(result, assetFeatureLambda(asset));
	}
	return result;
};

//============================================================================
const std::function<double(
	const std::shared_ptr<Asset>&,
	const std::vector<AssetLambda>& operations)> concrete_lambda_chain = [](
		const std::shared_ptr<Asset>& asset,
		const std::vector<AssetLambda>& operations
		)
{
	double result = 0;
	for (const auto& lambda : operations) {
		const auto& op = lambda.first;
		const auto& assetFeatureLambda = lambda.second;
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

	auto s = exchange_map->__get_dt_index().size();
	this->nlv_history.reserve(s);
	this->cash_history.reserve(s);
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
	j["is_live"] = this->is_live;
	j["strategy_id"] = this->strategy_id;
	j["strategy_type"] = this->strategy_type;
	j["allocation"] = this->portfolio_allocation;
	j["trading_window"] = trading_window_to_key_str(this->trading_window);
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
	// allow for manual disable of a strategy
	if (!this->is_live) { return false; }

	// check to see if the strategy has subsribed to an exchange and if the exchange took step
	if (!this->__exchange_step || !(*this->__exchange_step)) { return false; }

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
	ExchangeView const& exchange_view,
	double epsilon,
	bool clear_missing,
	std::optional<TradeExitPtr> exit,
	AllocType alloc_type)
{
	auto trade_ids = this->portfolio->get_strategy_positions(this->strategy_index);
	auto& allocation = exchange_view.view;

	// if clear_missing, clear and trades with asset index not in the allocation
	if (clear_missing && trade_ids.size())
	{
		std::vector<size_t> results;
		for (const auto& element : trade_ids) {
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
			this->place_market_order(asset_index, size, exit.value());
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
void AgisStrategy::place_market_order(
	size_t asset_index_,
	double units_,
	std::optional<TradeExitPtr> exit)
{
	this->router->place_order(std::make_unique<Order>(
		OrderType::MARKET_ORDER,
		asset_index_,
		units_,
		this->strategy_index,
		this->get_portfolio_index(),
		std::move(exit)
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
	order->set_limit(limit);
	this->router->place_order(std::move(order));
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


//============================================================================
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


//============================================================================
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
void AgisStrategyMap::__remove_strategy(std::string const& id)
{
	auto index = this->strategy_id_map.at(id);
	this->strategies.erase(index);
	this->strategy_id_map.erase(id);
}


//============================================================================
AGIS_API std::string opp_to_str(const AgisOperation& func)
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


//============================================================================
AGIS_API std::string alloc_to_str(AllocType alloc_type)
{
	static const std::map<AllocType, std::string> typeStrings = {
		{AllocType::UNITS, "UNITS"},
		{AllocType::DOLLARS, "DOLLARS"},
		{AllocType::PCT, "PCT"}
	};

	auto it = typeStrings.find(alloc_type);
	if (it != typeStrings.end()) return it->second;
	return "UNKNOWN";
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
	}
	this->strategy_allocate(
		ev,
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

	// set the strategy warmup period
	this->warmup = this->ev_lambda_struct.value().warmup;
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

class {STRATEGY_ID}Class : public AgisStrategy {
public:
	AGIS_STRATEGY_API {STRATEGY_ID}Class (
        PortfolioPtr const portfolio_
    ) : AgisStrategy("{STRATEGY_ID}Class", portfolio_, {ALLOC}) {
		this->strategy_type = AgisStrategyType::CPP;
		this->trading_window = {TRADING_WINDOW};
	};

    AGIS_STRATEGY_API inline static std::unique_ptr<AgisStrategy> create_instance(
        PortfolioPtr const& portfolio_
    ) 
	{
        return std::make_unique<{STRATEGY_ID}Class>(portfolio_);
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


	std::string asset_lambda = R"()";
	for (auto& pair : ev_lambda_ref.asset_lambda)
	{
		//TODO fix this
		std::string lambda_mid = R"(operations.emplace_back({OPP}, [&](const AssetPtr& asset) {
			return asset_feature_lambda(asset, "{COL}", {INDEX}).unwrap();
		});
)";
		auto pos = lambda_mid.find("{OPP}");
		lambda_mid.replace(pos, 5, opp_to_str(pair.l.first));
		pos = lambda_mid.find("{COL}");
		lambda_mid.replace(pos, 5, pair.column);
		pos = lambda_mid.find("{INDEX}");
		lambda_mid.replace(pos, 7, std::to_string(pair.row));
		asset_lambda = asset_lambda + lambda_mid;
	}
	std::string next_method = R"(auto next_lambda = [](const AssetPtr& asset) -> double {
		static std::vector<AssetLambda> operations;
		if (operations.empty()) {
		{LAMBDA_CHAIN}
		}
			
		double result = concrete_lambda_chain(
			asset, 
			operations
		);
		return result;
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

	// Replace the lambda chain
	pos = next_method.find("{LAMBDA_CHAIN}");
	next_method.replace(pos, 14, asset_lambda);

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

#include "{STRATEGY_ID}Class.h"

void {STRATEGY_ID}Class::build(){
	// set the strategies target exchanges
	{BUILD_METHOD}
};

void {STRATEGY_ID}Class::next(){
	if (this->exchange->__get_exchange_index() < this->warmup) { return; }
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

	// Replace strategy class name
	str_replace_all(strategy_source, place_holder, strategy_id);


	auto header_path = strat_folder / (strategy_id + "Class.h");
	auto source_path = strat_folder / (strategy_id + "Class.cpp");
	AGIS_TRY(code_gen_write(header_path, strategy_header))
	AGIS_TRY(code_gen_write(source_path, strategy_source))
}