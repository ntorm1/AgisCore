#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"
#include <utility>
#include <filesystem>

#include "AgisEnums.h"
#include "AgisFunctional.h"
#include "AgisRisk.h"
#include "AgisAnalysis.h"
#include "AgisStrategyTracers.h"
#include "Order.h"
#include "Portfolio.h"

namespace fs = std::filesystem;

namespace Agis {
	class BrokerMap;
	class Broker;
	typedef std::shared_ptr<Broker> BrokerPtr;
};

class AgisStrategy;
class AgisRouter;
class Exchange;
class ExchangeMap;
struct ExchangeView;
typedef std::shared_ptr<Exchange> ExchangePtr;

using namespace Agis;



AGIS_API typedef std::unique_ptr<AgisStrategy> AgisStrategyPtr;


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
	friend class Broker;
	friend class AgisStrategyMap;
	friend class AgisStrategyTracers;
public:
	virtual ~AgisStrategy() = default;
	AgisStrategy() = default;
	AGIS_API AgisStrategy(
		std::string id,
		PortfolioPtr const portfolio_,
		BrokerPtr broker_,
		double portfolio_allocation_
	);

	AGIS_API inline static void __reset_counter() { strategy_counter.store(0); }

	/// <summary>
	/// Pure virtual function to be called on every time step
	/// Note: must be public for trampoline Py Class and AgisStrategy.dll classes
	/// </summary> 
	virtual void next() = 0;

	/// <summary>
	/// Pure virtual function to be called on reset of Hydra instance
	/// Note: must be public for trampoline Py Class and AgisStrategy.dll classes
	/// </summary>
	virtual void reset() = 0;

	/// <summary>
	/// Pure virtual function to be called after the portfolio and exchangemap have beend built
	/// Note: must be public for trampoline Py Class and AgisStrategy.dll classes
	/// </summary>
	virtual void build() = 0;

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

	/// <summary>
	/// Base serialization of the AgisStrategy class
	/// </summary>
	/// <param name="j"></param>
	AGIS_API virtual std::expected<rapidjson::Document, AgisException> to_json() const;

	/// <summary>
	/// Restore a strategy from a give filepath 
	/// </summary>
	/// <param name="path"></param>
	/// <returns></returns>
	AGIS_API inline virtual void restore(fs::path path) {};
	
	AGIS_API [[nodiscard]] AgisResult<bool> exchange_subscribe(std::string const& exchange_id);

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
	/// Function called before step() to validate wether the strategy will make a step
	/// forward in time.
	/// </summary>
	bool __is_step();

	/// <summary>
	/// Remember a historical order that the strategy placed
	/// </summary>
	/// <param name="order"></param>
	void __remember_order(SharedOrderPtr order) { this->order_history.push_back(order); }
	
	/**
	 * @brief function to allow for a portfolio to insert a new trade into it's parent strategy
	 * @param trade new trade to insert
	*/
	void __add_trade(SharedTradePtr trade) { this->trades.insert({ trade->asset_index, trade }); };
	
	/**
	 * @brief remove an existing trade from a strategy's portfolio, used by the parent portfolio for managing trades.
	 * @param asset_index unique index of the asset to remove
	*/
	void __remove_trade(size_t asset_index) { this->trades.erase(asset_index); };

	/**
	 * @brief Take a closed trade and insert it into the strategy's trade history
	 * @param trade closed trade to insert
	*/
	void __remember_trade(SharedTradePtr trade) { this->trade_history.push_back(trade); }

	/**
	 * @brief Validates a new pending order attempting to be placed by the strategy
	 * @param order that is being attempt to be placed
	 * @return wether or not the order is valid
	*/
	AGIS_API void __validate_order(OrderPtr& order);


	/// <summary>
	/// Get all orders that have been  placed by the strategy
	/// </summary>
	/// <returns></returns>
	AGIS_API inline std::vector<SharedOrderPtr> const& get_order_history() const { return this->order_history; }
	
	AGIS_API inline std::vector<SharedTradePtr> const& get_trade_history() const { return this->trade_history; }


	AGIS_API virtual AgisResult<bool> set_beta_trace(bool val, bool check = true);
	AGIS_API virtual AgisResult<bool> set_net_leverage_trace(bool val);
	AGIS_API virtual AgisResult<bool> set_vol_trace(bool val);
	AGIS_API virtual AgisResult<bool> set_beta_scale_positions(bool val, bool check = true) {apply_beta_scale = val; return AgisResult<bool>(true);}
	AGIS_API virtual AgisResult<bool> set_beta_hedge_positions(bool val, bool check = true) {apply_beta_hedge = val; return AgisResult<bool>(true);}

	double get_nlv() const noexcept { return this->tracers.nlv.load(); }
	double get_cash() const noexcept { return this->tracers.cash.load(); }
	double get_allocation() const noexcept { return this->portfolio_allocation; }
	size_t get_step_frequency() const noexcept  { return this->step_frequency.value_or(1); }

	AGIS_API std::optional<double> get_max_leverage() const { return this->limits.max_leverage; }
	AGIS_API std::optional<double> get_net_leverage_ratio() const;
	AGIS_API std::optional<double> get_net_beta() const { return this->tracers.get(Tracer::BETA);}
	AGIS_API std::optional<double> get_portfolio_volatility() const { return this->tracers.get(Tracer::VOLATILITY); }

	size_t get_strategy_index() const { return this->strategy_index; }
	size_t get_portfolio_index() const { return this->portfolio->__get_index(); }
	size_t get_broker_index() const;
	AGIS_API [[nodiscard]] PortfolioPtr const get_portfolio() const { return this->portfolio; }
	std::string const& get_strategy_id() const { return this->strategy_id; }
	std::string get_portfolio_id() const { return this->portfolio->__get_portfolio_id(); }
	Frequency get_frequency() const { return this->frequency; }


	AGIS_API void zero_out_tracers() { this->tracers.zero_out_tracers(); }

	/// <summary>
	/// Get the unique strategy id of a strategy instance
	/// </summary>
	/// <returns> unique strategy id of a strategy instance</returns>
	AgisStrategyType get_strategy_type() const { return this->strategy_type; }

	/// <summary>
	/// Get an exchange ptr by unique id 
	/// </summary>
	/// <param name="id">unique id of the exchange to get</param>
	/// <returns>Shared pointer to the exchange</returns>
	AGIS_API ExchangePtr const get_exchange() const;

	/// <summary>
	/// Get const pointer to the exchange map
	/// </summary>
	/// <param name="id">unique id of the exchange to get</param>
	/// <returns>Shared pointer to the exchange</returns>
	AGIS_API ExchangeMap const* get_exchanges() const { return this->exchange_map; };

	/// <summary>
	/// Set the window in which a strategy can trade. Endpoints are included
	/// </summary>
	/// <param name="w"></param>
	void set_trading_window(std::optional<
		std::pair<TimePoint, TimePoint> const> w) {this->trading_window = w;}

	/**
	 * @brief set the max leverage of the strategy, requires net leverage tracing
	 * @param max_leverage max leverage of the strategy
	*/
	AGIS_API inline void set_max_leverage(std::optional<double> max_leverage) { 
		this->limits.max_leverage = max_leverage; 
		this->set_net_leverage_trace(true);
	}

	/**
	 * @brief set the frequency in which the strategy calls virtual next method
	 * @param step_frequency_ frequency in which the strategy calls virtual next method
	*/
	void set_step_frequency(std::optional<size_t> step_frequency_) { this->step_frequency = step_frequency_; }

	/**
	 * @brief set the target leverage of the strategy
	 * @param t target leverage of the strategy
	*/
	AGIS_API void set_target(std::optional<double> t, AllocTypeTarget type_target = AllocTypeTarget::LEVERAGE);

	/**
	 * @brief remove the disabled flag from a strategy. This is done on the completion of a hydra run, and 
	 * is the result of a strategy being disabled during a hydra run due to breaching risk limits.
	 * @param val is the strategy disabled
	*/
	void __set_is_disabled(bool val) { this->is_disabled = val; }

	/// <summary>
	/// Set the trading window from prefined string 
	/// </summary>
	/// <param name="window_name"></param>
	[[nodiscard]] AgisResult<bool> set_trading_window(std::string const& window_name);

	/// <summary>
	/// Get the strategies current trading window
	/// </summary>
	/// <returns></returns>
	inline std::optional<TradingWindow> get_trading_window() const { return this->trading_window; };

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

	bool __is_disabled() const {return this->is_disabled;}

	/**
	 * @brief Build the strategy, called once registered to a hydra instance
	 * @param router_ 
	 * @param broker_map 
	*/
	void __build(AgisRouter* router_, Agis::BrokerMap* broker_map);

	bool __is_exchange_subscribed() const { return this->exchange_subsrciption != ""; }
	bool __is_beta_scaling() const { return this->apply_beta_scale; }
	bool __is_beta_hedged() const { return this->apply_beta_hedge; }
	bool __is_beta_trace() const { return this->tracers.has(Tracer::BETA); }
	bool __is_net_lev_trace() const {return this->tracers.has(Tracer::LEVERAGE); }
	bool __is_vol_trace() const {return this->tracers.has(Tracer::VOLATILITY); }
	AGIS_API inline void __set_allocation(double allocation_) { this->portfolio_allocation = allocation_; }
	AGIS_API inline void __set_exchange_map(ExchangeMap const* exchange_map_) { this->exchange_map = exchange_map_; }

	AGIS_API inline std::vector<double> get_beta_history() const { return tracers.beta_history; }
	AGIS_API inline std::vector<double> get_nlv_history() const { return tracers.nlv_history; }
	AGIS_API inline std::vector<double> get_cash_history() const { return tracers.cash_history;}
	AGIS_API inline std::vector<double> get_net_leverage_ratio_history() const { return tracers.net_leverage_ratio_history; }
	AGIS_API inline std::vector<double> const& get_portfolio_vol_vec() { return tracers.portfolio_volatility_history; }

	OrderPtr AGIS_API create_market_order(
		size_t asset_index,
		double units,
		std::optional<TradeExitPtr> exit = std::nullopt
	);

protected:
	/**
	 * @brief Attempts to send order to the router after validating it
	 * @param order unique pointer to the new order being placed
	 * @return 
	*/
	void AGIS_API place_order(OrderPtr order);

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

	/**
	 * @brief generate inverse order for each open trade and them to the router
	*/
	void clear_portfolio();

	/// <summary>
	/// Clear existing containers of all historical information
	/// </summary>
	AGIS_API virtual void __reset();

	/// <summary>
	/// Evaluate the portfolio at the current levels
	/// </summary>
	/// <param name="on_close">On the close of a time period</param>
	AgisResult<bool> __evaluate(bool on_close);

	/**
	 * @brief function called by trade when it is closed to zero out the portfolio weights of the given asset
	 * @param start_index 
	*/
	void __on_trade_closed(size_t asset_index);

	/// <summary>
	/// Get a trade by asset id
	/// </summary>
	/// <param name="asset_id">unique id of the asset to search for</param>
	/// <returns></returns>
	AGIS_API std::optional<SharedTradePtr> get_trade(std::string const& asset_id);

	/**
	 * @brief get const pointer the strategies portfolio weights
	*/
	AGIS_API AgisResult<VectorXd const*> get_portfolio_weights() const;

	/// <summary>
	/// Type of AgisStrategy
	/// </summary>
	AgisStrategyType strategy_type = AgisStrategyType::CPP;

	/// <summary>
	/// Frequency of strategy updates
	/// </summary>
	Frequency frequency = Frequency::Day1;

	/**
	 * @brief minimum rows needed to pass before calling strategy next
	*/
	size_t warmup = 0;

	/// <summary>
	/// Valid window in which the strategy next function is called
	/// </summary>
	std::optional<std::pair<TimePoint, TimePoint>> trading_window = std::nullopt;

	bool apply_beta_hedge = false;
	bool apply_beta_scale = false;

	AgisStrategyTracers tracers;

	ExchangeMap const* exchange_map = nullptr;
	Agis::BrokerMap* broker_map = nullptr;

	/**
	 * @brief Optional target leverage of the strategy
	*/
	std::optional<double> alloc_target = 1.0f;

	/**
	 * @brief optional target type of the strategy
	*/
	AllocTypeTarget alloc_type_target = AllocTypeTarget::LEVERAGE;

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

	bool is_order_validating = true;	///< wether or not to validate each incoming order
	bool is_live = true;				///< wether or not the strategy is currently live
	bool is_disabled = false;			///< is the strategy currently disabled due to violation of risk parameters 

	std::string exchange_subsrciption = "";
	ExchangePtr exchange = nullptr;
	BrokerPtr broker = nullptr;

	/**
	* @brief Pointer to the exchange's step boolean telling us wether or not the subscribed 
	* exchange stepped forward in time
	*/
	bool* __exchange_step = nullptr;

	std::optional<size_t> step_frequency = std::nullopt; /// the frequency in which to call the strategy next function	
	size_t strategy_index;		/// unique index of the strategy
	std::string strategy_id;	/// unique string id of the strategy

	/**
	 * @brief struct containing all risk parameters and limitations of the strategy 
	*/
	AgisRiskStruct limits;

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
	~AgisStrategyMap();

	/**
	 * @brief Remove a strategy form the strategt map by it's unique id
	 * @param id unique id of the strategy to remove
	 * @return 
	*/
	AGIS_API void __remove_strategy(std::string const& id);

	/**
	 * @brief get the unique id of a strategy by it's index
	 * @param index index of the strategy to get the id of
	 * @return 
	*/
	AGIS_API AgisResult<std::string> __get_strategy_id(size_t index) const;

	/**
	 * @brief get a vector of all the strategy ids currently registered in the strategy map
	 * @return vector of all the strategy ids currently registered in the strategy map
	*/
	AGIS_API std::vector<std::string> __get_strategy_ids() const;

	/**
	 * @brief get the strategy index by it's unique id
	 * @param id unique id of the strategy to get the index of
	 * @return index of the strategy
	*/
	AGIS_API inline size_t __get_strategy_index(std::string const& id) const { return this->strategy_id_map.at(id); }
	
	/**
	 * @brief register a new strategy to the strategy map
	 * @param strategy unique pointer to the strategy to register
	 * @return 
	*/
	AGIS_API void register_strategy(AgisStrategyPtr strategy);

	/**
	 * @brief get a raw pointer to a strategy by it's unique id
	 * @param strategy_id strategy id of the strategy to get
	 * @return raw pointer to the strategy
	*/
	AgisStrategy const* get_strategy(std::string const& strategy_id) const;

	/**
	 * @brief get a raw pointer to a non const strategy by it's unique id. This is used for the hydra to modify the strategy
	 * @param strategy_id strategy id of the strategy to get
	 * @return 
	*/
	AgisStrategy* __get_strategy(std::string const& strategy_id) const;

	/**
	 * @brief get a const reference to the strategy map containg all the strategies
	 * @return const reference to the strategy map
	*/
	ankerl::unordered_dense::map<size_t, AgisStrategyPtr> const& __get_strategies() const { return this->strategies; }
	
	/**
	 * @brief get a non const ref to the strategy map containing all the strategies. This is used for the hydra to modify the strategies
	 * @return  non const ref to the strategy map
	*/
	ankerl::unordered_dense::map<size_t, AgisStrategyPtr> & __get_strategies_mut() { return this->strategies; }

	/**
	 * @brief call virtual next method of each strategy in the strategy map if it is a valid step.
	 * @return wether or not any strategies took a step
	*/
	bool __next();

	/**
	 * @brief reset all strategies in the strategy map and call virtual reset method of each strategy
	*/
	void __reset();

	/**
	 * @brief remove all strategies from the strategy map
	*/
	void __clear();

	/**
	 * @brief call the virtual build method of each strategy
	 * @return wether or not all strategies were built successfully
	*/
	[[nodiscard]] AgisResult<bool> build();

	/**
	 * @brief does a strategy exist in the strategy map by it's unique id
	 * @param id unique id of the strategy to check for
	 * @return wether or not the strategy exists
	*/
	bool __strategy_exists(std::string const& id) const { return this->strategy_id_map.count(id) > 0; }

private:
	ankerl::unordered_dense::map<std::string, size_t> strategy_id_map;
	ankerl::unordered_dense::map<size_t, AgisStrategyPtr> strategies;

};


class BenchMarkStrategy : public AgisStrategy {
public:
	BenchMarkStrategy(
		PortfolioPtr const& portfolio_,
		BrokerPtr broker_,
		std::string const& strategy_id_
	) : AgisStrategy(strategy_id_, portfolio_, broker_, 1.0) {
		this->strategy_type = AgisStrategyType::BENCHMARK;
	}

	AGIS_API void evluate();

	AGIS_API void next() override;

	AGIS_API void build() override;

	AGIS_API inline void reset() override { this->i = 0; }

	AGIS_API inline void set_asset_id(std::string const& asset_id) { this->asset_id = asset_id; }

	/**
	 * @brief unique id of the benchmark asset
	*/
	std::string asset_id = "";

	/**
	 * @brief unique index of the benchmark asset
	*/
	size_t asset_index = 0;

	/**
	 * @brief integer to signal if this is the first step of the strategy, in which case we buy the benchmark asset
	*/
	int i = 0;
};

AGIS_API std::string trading_window_to_key_str(std::optional<TradingWindow> input_window_opt);
AGIS_API void str_replace_all(std::string& source, const std::string& oldStr, const std::string& newStr);
AGIS_API void code_gen_write(fs::path filename, std::string const& source);
