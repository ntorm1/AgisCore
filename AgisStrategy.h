#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"
#include <utility>
#include <filesystem>
#include "AgisRouter.h"
#include "AgisAnalysis.h"
#include "Order.h"
#include "Portfolio.h"
#include "Exchange.h"

namespace fs = std::filesystem;

class AgisStrategy;

AGIS_API typedef std::unique_ptr<AgisStrategy> AgisStrategyPtr;

AGIS_API typedef std::function<double(double, double)> AgisOperation;
AGIS_API extern const AgisOperation agis_init;
AGIS_API extern const AgisOperation agis_identity;
AGIS_API extern const AgisOperation agis_add;
AGIS_API extern const AgisOperation agis_subtract;
AGIS_API extern const AgisOperation agis_multiply;
AGIS_API extern const AgisOperation agis_divide;
AGIS_API std::string opp_to_str(const AgisOperation& func);

AGIS_API typedef std::pair<AgisOperation, std::function<AgisResult<double>(const std::shared_ptr<Asset>&)>> AssetLambda;
struct AGIS_API AssetLambdaScruct {
	AssetLambda l;
	AgisOperation opp;
	std::string column;
	int row;
};

AGIS_API typedef std::vector<AssetLambdaScruct> AgisAssetLambdaChain;
AGIS_API typedef std::function<double(AssetPtr const&)> ExchangeViewOperation;
AGIS_API typedef std::function<ExchangeView(
	AgisAssetLambdaChain const&,
	ExchangePtr const,
	ExchangeQueryType,
	int)
> ExchangeViewLambda;


enum class AGIS_Function {
	INIT,		// returns the element in the second position
	IDENTITY,	// returns the element in the first position
	ADD,		// addition
	SUBTRACT,	// subtraction
	MULTIPLY,	// multiply
	DIVIDE		// divide
};


enum class AGIS_API AgisStrategyType {
	CPP,			// a c++ based strategy directly inheriting from AgisStrategy
	FLOW,			// a flow strategy used by the Nexus Node Editor to create Abstract strategies
	PY,				// a python strategy tramploine off PyAgisStrategy from AgisCorePy
	BENCHMARK,		// a benchmark strategy that does not interfer with portfolio values
};


NLOHMANN_JSON_SERIALIZE_ENUM(AgisStrategyType, {
	{AgisStrategyType::CPP, "CPP"},
	{AgisStrategyType::FLOW, "FLOW"},
	{AgisStrategyType::PY, "PY"},
	{AgisStrategyType::BENCHMARK, "BENCHMARK"}
	})


enum class AGIS_API AllocType
{
	UNITS,		// set strategy portfolio to have N units
	DOLLARS,	// set strategy portfolio to have $N worth of units
	PCT			// set strategy portfolio to have %N worth of units (% of nlv)
};
AGIS_API std::string alloc_to_str(AllocType alloc_type);


AGIS_API typedef const std::pair<TimePoint, TimePoint> TradingWindow;

extern AGIS_API TradingWindow us_equity_reg_hrs;
extern AGIS_API TradingWindow all_hrs;

extern AGIS_API std::unordered_map<std::string, AgisOperation> agis_function_map;
extern AGIS_API std::vector<std::string> agis_function_strings;
extern AGIS_API std::unordered_map<std::string, ExchangeQueryType> agis_query_map;
extern AGIS_API std::vector<std::string> agis_query_strings;
extern AGIS_API std::vector<std::string> agis_strat_alloc_strings;
extern AGIS_API std::vector<std::string> agis_trading_windows;
extern AGIS_API std::unordered_map<std::string, TradingWindow> agis_trading_window_map;


struct AGIS_API StrategyAllocLambdaStruct {
	/// <summary>
	/// The minimum % difference between target position size and current position size
	/// needed to trigger a new order
	/// </summary>
	double epsilon;

	/// <summary>
	/// The targert portfolio leverage used to apply weights to the allocations
	/// </summary>
	double target_leverage;

	/// <summary>
	/// Optional extra param to pass to ev weight application function
	/// </summary>
	std::optional<double> ev_extra_opp = std::nullopt;

	/// <summary>
	/// Clear any positions that are currently open but not in the allocation
	/// </summary>
	bool clear_missing;

	/// <summary>
	/// Type of weights to apply to the exchange view
	/// </summary>
	std::string ev_opp_type;

	/// <summary>
	/// Type of allocation to use 
	/// </summary>
	AllocType alloc_type;
};

struct AGIS_API ExchangeViewLambdaStruct {
	int N;
	size_t warmup;
	AgisAssetLambdaChain asset_lambda;
	ExchangeViewLambda exchange_view_labmda;
	ExchangePtr exchange;
	ExchangeQueryType query_type;
	std::optional<StrategyAllocLambdaStruct> strat_alloc_struct = std::nullopt;
};


AGIS_API typedef std::function<double(
	double a,
	double b
	)> Operation;


extern AGIS_API const std::function<AgisResult<double>(
	const std::shared_ptr<Asset>&,
	const std::vector<AssetLambdaScruct>& operations)> asset_feature_lambda_chain;
extern AGIS_API const std::function<AgisResult<double>(
	const std::shared_ptr<Asset>&,
	const std::vector<AssetLambda>& operations)> concrete_lambda_chain;

extern AGIS_API std::unordered_map<std::string, AllocType> agis_strat_alloc_map;


class AgisStrategy
{
	friend struct Trade;
	friend class Portfolio;
public:
	virtual ~AgisStrategy() = default;
	AgisStrategy() = default;
	AgisStrategy(
		std::string id, 
		PortfolioPtr const portfolio_,
		double portfolio_allocation_
	):
		portfolio(portfolio_)
	{
		this->strategy_id = id;
		this->strategy_index = strategy_counter++;
		this->router = nullptr;

		this->portfolio_allocation = portfolio_allocation_;
		this->nlv = portfolio_allocation * portfolio->get_cash();
		this->cash = portfolio_allocation * portfolio->get_cash();
		this->starting_cash = this->cash;
	}

	/// <summary>
	/// Pure virtual function to be called on every time step
	/// </summary> 
	virtual void next() = 0;

	/// <summary>
	/// Pure virtual function to be called on reset of Hydra instance
	/// </summary>
	virtual void reset() = 0;

	/// <summary>
	/// Pure virtual function to be called after the portfolio and exchangemap have beend built
	/// </summary>
	virtual void build() = 0;


	/// <summary>
	/// Base serialization of the AgisStrategy class
	/// </summary>
	/// <param name="j"></param>
	AGIS_API virtual void to_json(json& j);

	/// <summary>
	/// Restore a strategy from a give filepath 
	/// </summary>
	/// <param name="path"></param>
	/// <returns></returns>
	AGIS_API inline virtual void restore(fs::path path) {};

	/// <summary>
	/// Subscribe to an exchange, next() will be called when that exchange steps
	/// </summary>
	/// <param name="exchange_id">Unique id of the exchange</param>
	AGIS_API [[nodiscard]] AgisResult<bool> exchange_subscribe(std::string const& exchange_id);

	/// <summary>
	/// Clear existing containers of all historical information
	/// </summary>
	AGIS_API virtual void __reset();

	/// <summary>
	/// Set wether or not the strategy is currently running
	/// </summary>
	/// <returns></returns>
	AGIS_API inline void set_is_live(bool _is_live) { this->is_live = _is_live; };

	/// <summary>
	/// Set the type of strategy, either c++, flow based (node editor), or py (pybind11)
	/// </summary>
	/// <param name="t">type of strategy</param>
	/// <returns></returns>
	AGIS_API inline void set_strategy_type(AgisStrategyType t) { this->strategy_type = t; }

	/// <summary>
	/// Allocate a strategie's portfolio give a vector of pairs of <asset index, allocation>
	/// </summary>
	/// <param name="allocation">A vector of pairs representing the allocaitons</param>
	/// <param name="epsilon">Minimum % difference in units needed to trigger new order</param>
	/// <param name="clear_missing">Clear existing positions not in the allocation</param>
	/// <param name="exit">Optional trade exit pointer</param>
	/// <param name="alloc_type">Type of allocation represented</param>
	/// <returns></returns>
	AGIS_API void strategy_allocate(
		ExchangeView& allocation,
		double epsilon,
		bool clear_missing = true,
		std::optional<TradeExitPtr> exit = std::nullopt,
		AllocType alloc_type = AllocType::UNITS
	);


	AGIS_API inline static void __reset_counter() { strategy_counter.store(0); }

	/// <summary>
	/// Build the strategy, called once registered to a hydra instance
	/// </summary>
	/// <param name="router_"></param>
	/// <param name="portfolo_map"></param>
	/// <param name="exchange_map"></param>
	void __build(AgisRouter* router_, ExchangeMap* exchange_map);

	/// <summary>
	/// Function called before step() to validate wether the strategy will make a step
	/// forward in time.
	/// </summary>
	bool __is_step();

	/// <summary>
	/// Remember a historical order that the strategy placed
	/// </summary>
	/// <param name="order"></param>
	void __remember_order(SharedOrderPtr order) { this->order_history.push_back(order); }
	void __add_trade(SharedTradePtr trade) { this->trades.insert({ trade->asset_index, trade }); };
	void __remove_trade(size_t asset_index) { this->trades.erase(asset_index); }
	void __remember_trade(SharedTradePtr trade) { this->trade_history.push_back(trade); }

	/// <summary>
	/// Evaluate the portfolio at the current levels
	/// </summary>
	/// <param name="on_close">On the close of a time period</param>
	void __evaluate(bool on_close);

	/// <summary>
	/// Get all orders that have been  placed by the strategy
	/// </summary>
	/// <returns></returns>
	AGIS_API inline std::vector<SharedOrderPtr> const& get_order_history() const { return this->order_history; }
	
	AGIS_API inline std::vector<SharedTradePtr> const& get_trade_history() const { return this->trade_history; }

	/// <summary>
	/// Set the strategy to store the net beta of their positions at every time step
	/// </summary>
	/// <param name="val"></param>
	/// <param name="check"></param>
	/// <returns></returns>
	AGIS_API virtual AgisResult<bool> set_beta_trace(bool val, bool check = true);
	
	/// <summary>
	/// Trace the net leverage ratio of the strategy at every time step
	/// </summary>
	/// <param name="val"></param>
	/// <returns></returns>
	AGIS_API virtual AgisResult<bool> set_net_leverage_trace(bool val);
	
	/// <summary>
	/// Set the trategy to scale the positions by the beta of the asset when calling strategy_allocate
	/// </summary>
	/// <param name="val">wether or not to add beta scaling</param>
	/// <param name="check">validate beta asset</param>
	/// <returns></returns>
	AGIS_API virtual AgisResult<bool> set_beta_scale_positions(bool val, bool check = true) {apply_beta_scale = val; return AgisResult<bool>(true);}
	
	/// <summary>
	/// Generate beta hedge for the strategy when calling strategy allocate
	/// </summary>
	/// <param name="val">wether or not to add beta hedge</param>
	/// <param name="check">validate beta asset</param>
	/// <returns></returns>
	AGIS_API virtual AgisResult<bool> set_beta_hedge_positions(bool val, bool check = true) {apply_beta_hedge = val; return AgisResult<bool>(true);}

	double get_nlv() const { return this->nlv; }
	double get_cash() const { return this->cash; }
	double get_allocation() const { return this->portfolio_allocation; }

	/// <summary>
	/// Get the unique strategy index of a strategy instance
	/// </summary>
	/// <returns> unique strategy index of a strategy instance </returns>
	size_t get_strategy_index() const { return this->strategy_index; }

	/// <summary>
	/// Get the unique strategy id of a strategy instance
	/// </summary>
	/// <returns> unique strategy id of a strategy instance</returns>
	std::string get_strategy_id() const { return this->strategy_id; }

	/// <summary>
	/// Get the unique strategy id of a strategy instance
	/// </summary>
	/// <returns> unique strategy id of a strategy instance</returns>
	AgisStrategyType get_strategy_type() const { return this->strategy_type; }

	/// <summary>
	/// Get the unique strategy id of a strategy instance
	/// </summary>
	/// <returns> unique strategy id of a strategy instance</returns>
	Frequency get_frequency() const { return this->frequency; }

	/// <summary>
	/// Get the index of the portfolio the strategy is registered to 
	/// </summary>
	/// <returns>Unique index of the portfolio the strategy is registered to</returns>
	size_t get_portfolio_index() const { return this->portfolio->__get_index(); }

	/// <summary>
	/// Get the id of the portfolio the strategy is registered to 
	/// </summary>
	/// <returns>Unique id of the portfolio the strategy is registered to</returns>
	std::string get_portfolio_id() { return this->portfolio->__get_portfolio_id(); }

	/// <summary>
	/// Get an exchange ptr by unique id 
	/// </summary>
	/// <param name="id">unique id of the exchange to get</param>
	/// <returns>Shared pointer to the exchange</returns>
	AGIS_API ExchangePtr const get_exchange(std::string const& id) const;

	/// <summary>
	/// Set the window in which a strategy can trade. Endpoints are included
	/// </summary>
	/// <param name="w"></param>
	void set_trading_window(std::optional<
		std::pair<TimePoint, TimePoint> const> w) {this->trading_window = w;}

	/// <summary>
	/// Set the trading window from prefined string 
	/// </summary>
	/// <param name="window_name"></param>
	[[nodiscard]] AgisResult<bool> set_trading_window(std::string const& window_name);

	/// <summary>
	/// Get the strategies current trading window
	/// </summary>
	/// <returns></returns>
	inline std::optional<TradingWindow> get_trading_window() { return this->trading_window; };

	/// <summary>
	/// Find out if the class is and Abstract Agis Class
	/// </summary>
	/// <returns></returns>
	bool __is_abstract_class() const { return this->strategy_type == AgisStrategyType::FLOW; }

	/// <summary>
	/// Find out if the strategy is currently live in the hydra instance. Used for stepping and compiliing
	/// Only a strategy that is live will have code gen run. Else it will recompile what is in it.
	/// </summary>
	AGIS_API bool __is_live() const { return this->is_live; }

	/// <summary>
	/// Each unique trade is incrementally evaluated and the trade updates the valuation of it's parent strategy
	/// As such, at the start of an evaluation, the strategy valuation is zeroed out so the trades can increment it properly
	/// </summary>
	void __zero_out_tracers();

	bool __is_exchange_subscribed() const { return this->exchange_subsrciption == ""; }
	bool __is_beta_scaling() const { return this->apply_beta_scale; }
	bool __is_beta_hedged() const { return this->apply_beta_hedge; }
	bool __is_beta_trace() const { return this->net_beta.has_value(); }
	bool __is_net_lev_trace() const {return this->net_leverage_ratio.has_value(); } 
	void __set_allocation(double allocation) { this->portfolio_allocation = allocation; }

	AGIS_API inline std::vector<double> get_beta_history() const { return beta_history; }
	AGIS_API inline std::vector<double> get_nlv_history() const { return nlv_history; }
	AGIS_API inline std::vector<double> get_cash_history() const { return cash_history;}
	AGIS_API inline std::vector<double> get_net_leverage_ratio_history() const { return net_leverage_ratio_history; }

protected:
	/// <summary>
	/// Type of AgisStrategy
	/// </summary>
	AgisStrategyType strategy_type = AgisStrategyType::CPP;

	/// <summary>
	/// Frequency of strategy updates
	/// </summary>
	Frequency frequency = Frequency::Day1;

	void AGIS_API place_market_order(
		size_t asset_index,
		double units,
		std::optional<TradeExitPtr> exit = std::nullopt
	);
	void AGIS_API place_market_order(
		std::string const& asset_id,
		double units,
		std::optional<TradeExitPtr> exit = std::nullopt
	);
	void AGIS_API place_limit_order(
		size_t asset_index,
		double units,
		double limit,
		std::optional<TradeExitPtr> exit = std::nullopt
	);


	/// <summary>
	/// Get a trade by asset id
	/// </summary>
	/// <param name="asset_id">unique id of the asset to search for</param>
	/// <returns></returns>
	AGIS_API std::optional<SharedTradePtr> get_trade(std::string const& asset_id);

	/// <summary>
	/// Valid window in which the strategy next function is called
	/// </summary>
	std::optional<std::pair<TimePoint, TimePoint>> trading_window = std::nullopt;

	bool apply_beta_hedge = false;
	bool apply_beta_scale = false;

	std::vector<double> beta_history;
	std::vector<double> nlv_history;
	std::vector<double> cash_history;
	std::vector<double> net_leverage_ratio_history;

	/// <summary>
	/// Defined as the net beta dollars of the portfolio. I.e. units*share_price*beta
	/// </summary>
	std::optional<double> net_beta = std::nullopt;

	/// <summary>
	/// Defined as the net leverage ratio of the portfolio. I.e. (nlv - cash)/nlv
	/// </summary>
	std::optional<double> net_leverage_ratio = std::nullopt;

	double nlv = 0;
	double cash = 0;
	double starting_cash = 0;

	/// <summary>
	/// Pointer to the main exchange map object
	/// </summary>
	ExchangeMap const* exchange_map = nullptr;

	/// <summary>
	/// Optional target leverage of the strategy
	/// </summary>
	std::optional<double> target_leverage = std::nullopt;

	/// <summary>
	/// Mapping between asset_index and trade owned by the strategy
	/// </summary>
	ankerl::unordered_dense::map<size_t, SharedTradePtr> trades;

private:
	/// <summary>
	/// Counter of strategy instances
	/// </summary>
	AGIS_API static std::atomic<size_t> strategy_counter;

	/// <summary>
	/// Pointer to the Agis order router
	/// </summary>
	AgisRouter* router;

	/// <summary>
	///	Parent portfolio of the Agis Strategy
	/// </summary>
	PortfolioPtr const portfolio;
	
	/// <summary>
	/// All historical orders placed by the strategy
	/// </summary>
	std::vector<SharedOrderPtr> order_history;

	double unrealized_pl = 0;
	double realized_pl = 0;
	double portfolio_allocation = 0;

	bool is_live = true;
	bool is_subsribed = false; 

	std::string exchange_subsrciption = "";

	/// <summary>
	/// Pointer to the exchange's step boolean telling us wether or not the subscribed 
	/// exchange stepped forward in time
	/// </summary>
	bool* __exchange_step = nullptr;
	
	/// <summary>
	/// Unique numerical rep of the strategy id to prevent string copies everywhere
	/// </summary>
	size_t strategy_index;

	/// <summary>
	/// Unique id of the strategy
	/// </summary>
	std::string strategy_id;

	/// <summary>
	/// Get current open position in a given asset by asset index
	/// </summary>
	/// <param name="asset_index">unique index of the asset to search for</param>
	/// <returns></returns>
	AGIS_API std::optional<SharedTradePtr> get_trade(size_t asset_index);

	std::vector<SharedTradePtr> trade_history;
};


class AgisStrategyMap
{
public:
	AgisStrategyMap() = default;

	AGIS_API void __remove_strategy(std::string const& id);
	AGIS_API AgisResult<std::string> __get_strategy_id(size_t index) const;
	AGIS_API std::vector<std::string> __get_strategy_ids() const;
	AGIS_API inline size_t __get_strategy_index(std::string const& id) const { return this->strategy_id_map.at(id); }
	AGIS_API void register_strategy(AgisStrategyPtr strategy);
	AgisStrategy const* get_strategy(std::string strategy_id) const;
	AgisStrategy* __get_strategy(std::string strategy_id) const;
	ankerl::unordered_dense::map<size_t, AgisStrategyPtr> const& __get_strategies() const { return this->strategies; }
	ankerl::unordered_dense::map<size_t, AgisStrategyPtr> & __get_strategies_mut() { return this->strategies; }

	bool __next();
	void __reset();
	void __clear();
	[[nodiscard]] AgisResult<bool> __build();

	bool __strategy_exists(std::string const& id) const { return this->strategy_id_map.count(id) > 0; }

private:
	ankerl::unordered_dense::map<std::string, size_t> strategy_id_map;
	ankerl::unordered_dense::map<size_t, AgisStrategyPtr> strategies;

};



class AbstractAgisStrategy : public AgisStrategy {
public:
	using AbstractExchangeViewLambda = std::function<
		std::optional<ExchangeViewLambdaStruct>
		()>;

	AbstractAgisStrategy(
		PortfolioPtr const& portfolio_,
		std::string const& strategy_id,
		double allocation
	) : AgisStrategy(strategy_id, portfolio_, allocation) {
		this->strategy_type = AgisStrategyType::FLOW;
	}

	AGIS_API void next() override;
	
	AGIS_API inline void reset() override {}

	AGIS_API void build() override;

	AGIS_API [[nodiscard]] AgisResult<bool> extract_ev_lambda();

	AGIS_API inline void set_abstract_ev_lambda(std::function<
		std::optional<ExchangeViewLambdaStruct>
		()> f_) { this->ev_lambda = f_; };

	AGIS_API void restore(fs::path path) override;

	AGIS_API void to_json(json& j);

	AGIS_API void code_gen(fs::path strat_folder);

	AGIS_API [[nodiscard]] AgisResult<bool> set_beta_trace(bool val, bool check = true);
	AGIS_API [[nodiscard]] AgisResult<bool> set_beta_scale_positions(bool val, bool check = true) override;
	AGIS_API [[nodiscard]] AgisResult<bool> set_beta_hedge_positions(bool val, bool check = true) override;
	AgisResult<bool> validate_market_asset();


private:
	AbstractExchangeViewLambda ev_lambda;
	std::optional<ExchangeViewLambdaStruct> ev_lambda_struct = std::nullopt;
	std::optional<double> ev_opp_param = std::nullopt;
	ExchangeViewOpp ev_opp_type = ExchangeViewOpp::UNIFORM;

	/// <summary>
	/// The number if steps need to happen on the target exchange before the 
	/// strategy next() method is called
	/// </summary>
	size_t warmup = 0;
};


class BenchMarkStrategy : public AgisStrategy {
public:
	BenchMarkStrategy(
		PortfolioPtr const& portfolio_,
		std::string const& strategy_id_
	) : AgisStrategy(strategy_id_, portfolio_, 1.0) {
		this->strategy_type = AgisStrategyType::BENCHMARK;
	}

	AGIS_API void evluate();

	AGIS_API void next() override;

	AGIS_API void build() override;

	AGIS_API inline void reset() override { this->i = 0; }

	AGIS_API inline void set_asset_id(std::string const& asset_id) { this->asset_id = asset_id; }

	std::string asset_id = "";
	size_t asset_index = 0;
	int i = 0;
};

AGIS_API std::string trading_window_to_key_str(std::optional<TradingWindow> input_window_opt);
AGIS_API void str_replace_all(std::string& source, const std::string& oldStr, const std::string& newStr);
AGIS_API void code_gen_write(fs::path filename, std::string const& source);
