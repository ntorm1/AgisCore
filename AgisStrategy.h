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
#include "Order.h"
#include "Portfolio.h"
#include "Exchange.h"

namespace fs = std::filesystem;

class AgisStrategy;
AGIS_API typedef std::unique_ptr<AgisStrategy> AgisStrategyPtr;
AGIS_API typedef std::reference_wrapper<AgisStrategyPtr> AgisStrategyRef;

AGIS_API typedef std::function<double(double, double)> AgisOperation;
AGIS_API extern const AgisOperation agis_init;
AGIS_API extern const AgisOperation agis_identity;
AGIS_API extern const AgisOperation agis_add;
AGIS_API extern const AgisOperation agis_subtract;
AGIS_API extern const AgisOperation agis_multiply;
AGIS_API extern const AgisOperation agis_divide;
AGIS_API std::string opp_to_str(const AgisOperation& func);

AGIS_API typedef std::pair<AgisOperation, std::function<double(const std::shared_ptr<Asset>&)>> AssetLambda;
struct AGIS_API AssetLambdaScruct {
	AssetLambda l;
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
	double epsilon;
	double target_leverage;
	bool clear_missing;
	std::string ev_opp_type;
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

AGIS_API typedef const std::function<AgisResult<double>(
	const std::shared_ptr<Asset>&,
	const std::string&,
	int
	)> AssetFeatureLambda;

extern AGIS_API AssetFeatureLambda asset_feature_lambda;


extern AGIS_API const std::function<double(
	const std::shared_ptr<Asset>&,
	const std::vector<AssetLambdaScruct>& operations)> asset_feature_lambda_chain;
extern AGIS_API const std::function<double(
	const std::shared_ptr<Asset>&,
	const std::vector<AssetLambda>& operations)> concrete_lambda_chain;

extern AGIS_API std::unordered_map<std::string, AllocType> agis_strat_alloc_map;


class AgisStrategy
{
public:
	AgisStrategy() = default;
	AgisStrategy(
		std::string id, 
		PortfolioPtr const portfolio_,
		double portfolio_allocation
	): portfolio(portfolio_) 
	{
		this->strategy_id = id;
		this->strategy_index = strategy_counter++;
		this->router = nullptr;
		this->portfolio_allocation = portfolio_allocation;
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
	AGIS_API void exchange_subscribe(std::string const& exchange_id);

	/// <summary>
	/// Clear existing containers of all historical information
	/// </summary>
	AGIS_API virtual void __reset();

	/// <summary>
	/// Set wether or not the strategy is currently running
	/// </summary>
	/// <returns></returns>
	AGIS_API inline void set_is_live(bool _is_live) { this->is_live = _is_live; };

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
	void __remember_order(OrderRef order) { this->order_history.push_back(order); }

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
	AGIS_API inline std::vector<OrderRef> const& get_order_history() const { return this->order_history; }
	
	AGIS_API inline std::vector<SharedTradePtr> const& get_trade_history() const { return this->trade_history; }

	void nlv_adjust(double nlv_adjustment) { this->nlv += nlv_adjustment; };
	void cash_adjust(double cash_adjustment) { this->cash += cash_adjustment; };
	void unrealized_adjust(double unrealized_adjustment) { this->unrealized_pl += unrealized_adjustment; };
	double get_nlv() { return this->nlv; }
	double get_allocation() { return this->portfolio_allocation; }

	/// <summary>
	/// Get the unique strategy index of a strategy instance
	/// </summary>
	/// <returns> unique strategy index of a strategy instance </returns>
	size_t get_strategy_index() { return this->strategy_index; }

	/// <summary>
	/// Get the unique strategy id of a strategy instance
	/// </summary>
	/// <returns> unique strategy id of a strategy instance</returns>
	std::string get_strategy_id() { return this->strategy_id; }

	/// <summary>
	/// Get the index of the portfolio the strategy is registered to 
	/// </summary>
	/// <returns>Unique index of the portfolio the strategy is registered to</returns>
	size_t get_portfolio_index() { return this->portfolio->__get_index(); }

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
	AgisResult<bool> set_trading_window(std::string const& window_name);

	inline std::optional<TradingWindow> get_trading_window() { return this->trading_window; };

	/// <summary>
	/// Find out if the class is and Abstract Agis Class
	/// </summary>
	/// <returns></returns>
	bool __is_abstract_class() const { return this->abstract_class; }

	/// <summary>
	/// Find out if the strategy is currently live in the hydra instance. Used for stepping and compiliing
	/// Only a strategy that is live will have code gen run. Else it will recompile what is in it.
	/// </summary>
	AGIS_API bool __is_live() const { return this->is_live; }

protected:
	bool abstract_class = false;

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

	/// <summary>
	/// Get current open position in a given asset by asset index
	/// </summary>
	/// <param name="asset_index">unique index of the asset to search for</param>
	/// <returns></returns>
	AGIS_API std::optional<TradeRef> get_trade(size_t asset_index);

	/// <summary>
	/// Get a trade by asset id
	/// </summary>
	/// <param name="asset_id">unique id of the asset to search for</param>
	/// <returns></returns>
	AGIS_API std::optional<TradeRef> get_trade(std::string const& asset_id);

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
		ExchangeView const& allocation,
		double epsilon,
		bool clear_missing = true,
		std::optional<TradeExitPtr> exit = std::nullopt,
		AllocType alloc_type = AllocType::UNITS
	);

	/// <summary>
	/// Valid window in which the strategy next function is called
	/// </summary>
	std::optional<std::pair<TimePoint, TimePoint>> trading_window = std::nullopt;


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
	/// Pointer to the main exchange map object
	/// </summary>
	ExchangeMap const* exchange_map = nullptr;

	/// <summary>
	/// All historical orders placed by the strategy
	/// </summary>
	std::vector<OrderRef> order_history;

	double unrealized_pl = 0;
	double realized_pl = 0;
	double nlv = 0;
	double cash = 0;
	double starting_cash = 0;
	double portfolio_allocation = 0;

	bool is_live = true;
	bool is_subsribed = false; 
	std::string exchange_subsrciption;
	/// <summary>
	/// Pointer to the exchange's step boolean telling us wether or not the subscribed 
	/// exchange stepped forward in time
	/// </summary>
	bool* __exchange_step = nullptr;


	size_t strategy_index;
	std::string strategy_id;

	std::vector<double> nlv_history;
	std::vector<double> cash_history;
	std::vector<SharedTradePtr> trade_history;
};


class AgisStrategyMap
{
public:
	AgisStrategyMap() = default;

	AGIS_API void __remove_strategy(std::string const& id);
	AGIS_API inline size_t __get_strategy_index(std::string const& id) { return this->strategy_id_map.at(id); }
	AGIS_API void register_strategy(AgisStrategyPtr strategy);
	const AgisStrategyRef get_strategy(std::string strategy_id);
	std::unordered_map<size_t, AgisStrategyPtr>& __get_strategies() { return this->strategies; }
	
	bool __next();
	void __reset();
	void __clear();
	void __build();

	bool __strategy_exists(std::string const& id) const { return this->strategy_id_map.count(id) > 0; }

private:
	std::unordered_map<std::string, size_t> strategy_id_map;
	std::unordered_map<size_t, AgisStrategyPtr> strategies;

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
		this->abstract_class = true;
	}

	AGIS_API void next() override;
	
	AGIS_API inline void reset() override {}

	AGIS_API void build() override;

	AGIS_API void extract_ev_lambda();

	AGIS_API inline void set_abstract_ev_lambda(std::function<
		std::optional<ExchangeViewLambdaStruct>
		()> f_) { this->ev_lambda = f_; };

	AGIS_API void restore(fs::path path) override;

	AGIS_API void to_json(json& j);

	AGIS_API void code_gen(fs::path strat_folder);

private:
	AbstractExchangeViewLambda ev_lambda;
	std::optional<ExchangeViewLambdaStruct> ev_lambda_struct = std::nullopt;
	ExchangeViewOpp ev_opp_type = ExchangeViewOpp::UNIFORM;

	/// <summary>
	/// The number if steps need to happen on the target exchange before the 
	/// strategy next() method is called
	/// </summary>
	size_t warmup = 0;
};

AGIS_API std::string trading_window_to_key_str(std::optional<TradingWindow> input_window_opt);
AGIS_API void str_replace_all(std::string& source, const std::string& oldStr, const std::string& newStr);
AGIS_API void code_gen_write(fs::path filename, std::string const& source);
AGIS_API void agis_realloc(ExchangeView* allocation, double c);