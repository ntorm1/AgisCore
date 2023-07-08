#pragma once
#include "pch.h"

#include "AgisRouter.h"
#include "Order.h"


class AgisStrategy
{
public:
	
	AgisStrategy() {
		this->strategy_index = strategy_counter++;
		this->router = nullptr;
	}

	/// <summary>
	/// Pure virtual function to be called on every time step
	/// </summary> 
	virtual void next() = 0;

	/// <summary>
	/// Build the strategy, called once registered to a hydra instance
	/// </summary>
	/// <param name="router_"></param>
	/// <param name="portfolo_map"></param>
	/// <param name="exchange_map"></param>
	void __build(AgisRouter* router_, PortfolioMap* portfolo_map, ExchangeMap* exchange_map);

	/// <summary>
	/// Get the unique index of the strategy
	/// </summary>
	/// <returns></returns>
	size_t get_strategy_index() { return this->strategy_index; }

protected:

	void place_market_order(
		size_t asset_index,
		double units,
		size_t portfilio_index
	);

private:
	static std::atomic<size_t> strategy_counter;

	/// <summary>
	/// Pointer to the Agis order router
	/// </summary>
	AgisRouter* router;

	/// <summary>
	/// Pointer to the main portfolio map object
	/// </summary>
	PortfolioMap* portfolo_map = nullptr;
	
	/// <summary>
	/// Pointer to the main exchange map object
	/// </summary>
	ExchangeMap* exchange_map = nullptr;

	size_t strategy_index;
};



class AgisStrategyMap
{
public:
	AgisStrategyMap() = default;

	void register_strategy(std::unique_ptr<AgisStrategy> strategy);
	
	void __next();


private:
	std::unordered_map<std::string, size_t> strategy_id_map;
	std::unordered_map<size_t, std::unique_ptr<AgisStrategy>> strategies;

};