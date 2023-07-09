#pragma once
#include "pch.h"

#include "AgisRouter.h"
#include "Order.h"
#include "Portfolio.h"

static std::atomic<size_t> strategy_counter(0);

class AgisStrategy;
AGIS_API typedef std::unique_ptr<AgisStrategy> AgisStrategyPtr;
AGIS_API typedef std::reference_wrapper<AgisStrategyPtr> AgisStrategyRef;

class AgisStrategy
{
public:
	
	AgisStrategy(std::string id, PortfolioPtr const& portfolio_): portfolio(portfolio_) {
		this->strategy_id = id;
		this->strategy_index = strategy_counter++;
		this->router = nullptr;
	}

	/// <summary>
	/// Pure virtual function to be called on every time step
	/// </summary> 
	virtual void next() = 0;

	/// <summary>
	/// Clear existing containers of all historical information
	/// </summary>
	AGIS_API virtual void __reset();

	/// <summary>
	/// Build the strategy, called once registered to a hydra instance
	/// </summary>
	/// <param name="router_"></param>
	/// <param name="portfolo_map"></param>
	/// <param name="exchange_map"></param>
	void __build(AgisRouter* router_, ExchangeMap* exchange_map);

	/// <summary>
	/// Remember a historical order that the strategy placed
	/// </summary>
	/// <param name="order"></param>
	void __remember_order(OrderRef order) { this->order_history.push_back(order); }

	/// <summary>
	/// Get all orders that have been  placed by the strategy
	/// </summary>
	/// <returns></returns>
	std::vector<OrderRef> const& get_order_history() const { return this->order_history; }
	
	size_t get_strategy_index() { return this->strategy_index; }
	std::string get_strategy_id() { return this->strategy_id; }
	size_t get_portfolio_index() { return this->portfolio->__get_index(); }

protected:
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
	/// Get strategies current open position in a given asset
	/// </summary>
	/// <param name="asset_index">unique id of the asset to search for</param>
	/// <returns></returns>
	AGIS_API std::optional<TradeRef> get_trade(size_t asset_index);
	AGIS_API std::optional<TradeRef> get_trade(std::string const& asset_id);


private:
	/// <summary>
	/// Pointer to the Agis order router
	/// </summary>
	AgisRouter* router;

	/// <summary>
	/// Pointer to the main portfolio map object
	/// </summary>
	PortfolioPtr const& portfolio;
	
	/// <summary>
	/// Pointer to the main exchange map object
	/// </summary>
	ExchangeMap* exchange_map = nullptr;

	/// <summary>
	/// All historical orders placed by the strategy
	/// </summary>
	std::vector<OrderRef> order_history;

	size_t strategy_index;
	std::string strategy_id;
};



class AgisStrategyMap
{
public:
	AgisStrategyMap() = default;

	void register_strategy(AgisStrategyPtr strategy);
	const AgisStrategyRef get_strategy(std::string strategy_id);
	
	void __next();
	void __reset();


private:
	std::unordered_map<std::string, size_t> strategy_id_map;
	std::unordered_map<size_t, AgisStrategyPtr> strategies;

};