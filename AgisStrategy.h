#pragma once
#include "pch.h"

#include "AgisRouter.h"
#include "Order.h"
#include "Portfolio.h"

static std::atomic<size_t> strategy_counter(0);

class AgisStrategy
{
public:
	
	AgisStrategy(PortfolioPtr const& portfolio_): portfolio(portfolio_) {
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
	void __build(AgisRouter* router_, ExchangeMap* exchange_map);


	size_t get_strategy_index() { return this->strategy_index; }
	size_t get_portfolio_index() { return this->portfolio->__get_index(); }

protected:
	void AGIS_API place_market_order(
		size_t asset_index,
		double units
	);
	void AGIS_API place_market_order(
		std::string asset_id,
		double units
	);

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