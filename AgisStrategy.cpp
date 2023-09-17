#include "pch.h"
#include <fstream>

#include <tbb/parallel_for_each.h>

#include "AgisAnalysis.h"
#include "AgisErrors.h"
#include "AgisStrategy.h"

std::atomic<size_t> AgisStrategy::strategy_counter(0);

//============================================================================
AgisStrategy::AgisStrategy(
	std::string id,
	PortfolioPtr const portfolio_,
	double portfolio_allocation_
) :
	portfolio(portfolio_),
	tracers(this)
{
	this->strategy_id = id;
	this->strategy_index = strategy_counter++;
	this->router = nullptr;

	this->portfolio_allocation = portfolio_allocation_;
	this->tracers.nlv = portfolio_allocation * portfolio->get_cash();
	this->tracers.cash = portfolio_allocation * portfolio->get_cash();
	this->tracers.starting_cash = this->tracers.cash;
	this->tracers.set(Tracer::CASH);
	this->tracers.set(Tracer::NLV);
}


//============================================================================
void AgisStrategy::__reset()
{
	this->trades.clear();
	this->order_history.clear();
	this->limits.__reset();
	this->tracers.reset_history();
	AGIS_TRY(this->reset());
}


//============================================================================
void AgisStrategy::__build(
	AgisRouter* router_
)
{
	this->router = router_;
	// init required tracing and limit values
	auto n = exchange_map->__get_dt_index().size();
	this->tracers.build(this, n);
	this->limits.__build(this);
}


//============================================================================
AgisResult<bool> AgisStrategy::__evaluate(bool on_close)
{
	// Note: at this point all trades have been evaluated and the cash balance has been updated
	// so we only have to observer the values or use them to calculate other values.
	if (on_close)
	{
		AGIS_DO_OR_RETURN(this->tracers.evaluate(), bool);
	}

	// if nlv < 0 clear the portfolio and disable the strategy
	if (this->tracers.nlv < 0)
	{
		this->clear_portfolio();
		this->is_disabled = true;
	}
	return AgisResult<bool>(true);
}


//============================================================================
void AgisStrategy::__on_trade_closed(size_t asset_index)
{
	// update the portfolio weights used to calculate vol to show that a trade has closed
	if (this->tracers.has(Tracer::VOLATILITY)) {
		this->tracers.set_portfolio_weight(asset_index, 0.0f);
	}
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
	j["beta_trace"] = this->tracers.has(Tracer::BETA);
	j["net_leverage_trace"] = this->tracers.has(Tracer::LEVERAGE);
	j["vol_trace"] = this->tracers.has(Tracer::VOLATILITY);

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
	if (!this->is_live || this->is_disabled) { return false; }

	// check to see if the strategy has subsribed to an exchange and if the exchange took step
	if (!this->__exchange_step || !(*this->__exchange_step)) { return false; }

	// check to see if the step frequency is set and if the current step is a multiple of the frequency
	if (this->step_frequency.has_value() && this->step_frequency.value() != 1) {
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
		cash_estimate = limits.estimate_phantom_cash(order.get());
		auto ratio = (this->tracers.nlv - (this->tracers.cash - this->limits.phantom_cash - cash_estimate)) / this->tracers.nlv;
		if (ratio > this->limits.max_leverage.value())
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

	//reflect the new units in the limits asset holdings
	this->limits.asset_holdings[order->get_asset_index()] += order->get_units();
	if (order->has_beta_hedge_order()) {
		auto& child_order = order->get_child_order_ref();
		this->limits.asset_holdings[child_order->get_asset_index()] += child_order->get_units();
	}
	if(this->tracers.has(Tracer::LEVERAGE)) this->tracers.net_leverage_ratio += cash_estimate;
}


//============================================================================
std::optional<SharedTradePtr> AgisStrategy::get_trade(std::string const& asset_id)
{
	auto asset_index = this->exchange_map->get_asset_index(asset_id);
	if (!this->trades.count(asset_index)) { return std::nullopt; }
	return this->trades.at(asset_index);
}


//============================================================================
AGIS_API ExchangePtr const AgisStrategy::get_exchange() const
{
	return this->exchange_map->get_exchange(this->exchange_subsrciption);
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
	if (this->apply_beta_hedge) AGIS_DO_OR_THROW(exchange_view.beta_hedge(this->alloc_target));
	if (this->alloc_type_target == AllocTypeTarget::VOL) AGIS_DO_OR_THROW(exchange_view.vol_target(this->alloc_target.value()));

	auto& allocation = exchange_view.view;

	// generate orders based on the allocation passed
	for (auto& alloc : allocation)
	{
		if (!alloc.live) continue;
		size_t asset_index = alloc.asset_index;
		double size = alloc.allocation_amount;
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
				size *=  (this->tracers.nlv / market_price);
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
		std::optional<TradeExitPtr> trade_exit_copy = std::nullopt;
		if (exit.has_value()) {
			auto exit_raw_ptr = exit.value()->clone();
			trade_exit_copy = std::make_optional<TradeExitPtr>(exit_raw_ptr);
		}

		// add beta hedge order if needed
		auto order = this->create_market_order(asset_index,size, trade_exit_copy);
		if (!alloc.beta_hedge_size.has_value()) {
			this->place_order(std::move(order));
		}
		else {
			// generate the beta hedge child order
			size_t market_asset_index = exchange_view.market_asset_index.value();
			double beta_hedge_order_size = alloc.beta_hedge_size.value() * (this->tracers.nlv / exchange_view.market_asset_price.value());
			double inverse_beta_hedge_order_size = -beta_hedge_order_size;
			// if there is an existing trade with an existing beta hedge subtract out this units
			auto trade_opt = this->get_trade(asset_index);
			if (trade_opt.has_value() && trade_opt.value()->get_child_partition(market_asset_index))
			{
				auto partition = trade_opt.value()->get_child_partition(market_asset_index);
				beta_hedge_order_size -= partition->child_trade_units;
			}
			auto beta_hedge_order = this->create_market_order(
				market_asset_index,
				beta_hedge_order_size,
				std::nullopt
			);

			// insert the child order into the main order to be filled on main order fill
			order->insert_beta_hedge_order(std::move(beta_hedge_order));

			// place the main order
			this->place_order(std::move(order));
		}
	}

	// if beta hedging touch the beta hedge trade manually as the asset is not in the allocation vector
	if (apply_beta_hedge)
	{
		auto market_index = exchange_view.exchange->__get_market_asset_struct().value().market_index;
		auto trade_opt = this->get_trade(market_index);
		if (trade_opt.has_value()) {
			trade_opt.value()->strategy_alloc_touch = true;
		}
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
void AgisStrategy::set_target(std::optional<double> t, AllocTypeTarget type_target)
{
	this->alloc_target = t;
	this->alloc_type_target = type_target;
	if(type_target == AllocTypeTarget::VOL) this->set_vol_trace(true);
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
void AgisStrategy::clear_portfolio()
{
	for (auto& [id, trade] : this->trades)
	{
		auto order = trade->generate_trade_inverse();
		this->place_order(std::move(order));
	}
}


//============================================================================
void AGIS_API AgisStrategy::place_order(OrderPtr order)
{
	if(is_order_validating) this->__validate_order(order);
	this->router->place_order(std::move(order));
}


//============================================================================
OrderPtr AgisStrategy::create_market_order(
	size_t asset_index_,
	double units_,
	std::optional<TradeExitPtr> exit)
{
	return std::make_unique<Order>(
		OrderType::MARKET_ORDER,
		asset_index_,
		units_,
		this->strategy_index,
		this->get_portfolio_index(),
		exit,
		this->strategy_type == AgisStrategyType::BENCHMARK
	);
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
		AGIS_TRY(strategy.second->next());
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
		AGIS_TRY(strategy.second->__reset());
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
AgisResult<bool> AgisStrategyMap::build()
{
	for (auto& strategy : this->strategies)
	{
		if (!strategy.second->__is_live()) continue;
		try {
			// build the strategy instance
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
	AgisStrategyPtr strategy = std::move(this->strategies.at(index));
	this->strategies.erase(index);
	this->strategy_id_map.erase(id);

	// check to see if the strategy is CPP strategy, in which case release unique pointer to prevent double 
	// free form the AgisStrategy.dll
	if (strategy->get_strategy_type() == AgisStrategyType::CPP) {
		AgisStrategy* raw_ptr = strategy.release();
	}
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
AgisResult<bool> AgisStrategy::set_beta_trace(bool val, bool check)
{
	if (val) this->tracers.set(Tracer::BETA);
	else this->tracers.reset(Tracer::BETA);

	auto benchmark = this->portfolio->__get_benchmark_strategy();
	if (benchmark && this->strategy_type != AgisStrategyType::BENCHMARK) { benchmark->set_beta_trace(val); }
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> AgisStrategy::set_net_leverage_trace(bool val)
{
	if (val) this->tracers.set(Tracer::LEVERAGE);
	else this->tracers.reset(Tracer::LEVERAGE);

	auto benchmark = this->portfolio->__get_benchmark_strategy();
	if (benchmark && this->strategy_type != AgisStrategyType::BENCHMARK) { benchmark->set_net_leverage_trace(val); }
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> AgisStrategy::set_vol_trace(bool val)
{
	// init the portfolio volatility tracer value
	if (val) this->tracers.set(Tracer::VOLATILITY);
	else this->tracers.reset(Tracer::VOLATILITY);

	auto benchmark = this->portfolio->__get_benchmark_strategy();
	if (benchmark && this->strategy_type != AgisStrategyType::BENCHMARK) {benchmark->set_vol_trace(val);}

	return AgisResult<bool>(true);
}


//============================================================================
std::optional<double> AgisStrategy::get_net_leverage_ratio() const
{
	if (!this->tracers.has(Tracer::LEVERAGE)) {
		return std::nullopt;
	}
	return this->tracers.net_leverage_ratio / this->tracers.nlv;
}


//============================================================================
AgisResult<VectorXd const*> AgisStrategy::get_portfolio_weights() const
{
	if (!this->tracers.portfolio_weights.size() == 0) {
		return AgisResult<VectorXd const*>(AGIS_EXCEP("missing weights vector"));
	}
	return AgisResult<VectorXd const*>(&this->tracers.portfolio_weights);
}