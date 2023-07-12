#pragma once

#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "pch.h"
#include <functional>
#include <limits>


#include "AgisPointers.h"
#include "AgisRouter.h"
#include "Exchange.h"
#include "Order.h"
#include "Trade.h"

class Portfolio;
class AgisStrategy;
struct Position;

AGIS_API typedef std::unique_ptr<AgisStrategy> AgisStrategyPtr;
AGIS_API typedef std::reference_wrapper<AgisStrategyPtr> AgisStrategyRef;

AGIS_API typedef std::vector<std::pair<size_t, double>> StrategyAllocation;
AGIS_API typedef std::unique_ptr<Portfolio> PortfolioPtr;
AGIS_API typedef std::unique_ptr<Position> PositionPtr;
AGIS_API typedef std::reference_wrapper<const PositionPtr> PositionRef;


struct Position
{
    size_t position_id;
    size_t asset_id;
    size_t portfolio_id;

    double close_price = 0;
    double average_price;
    double last_price = 0;

    double unrealized_pl = 0;
    double realized_pl = 0;
    double nlv = 0;
    double units = 0;

    long long position_open_time;
    long long position_close_time = 0;

    /// <summary>
    /// Bars held including the bar it was placed on. I.e. if placed on close on Jan 1st,
    /// bars_held will equal 1 on close of Jan 1st bar.
    /// </summary>
    size_t bars_held = 0;

    Position(AgisStrategyRef strategy, OrderPtr const& order);

    std::optional<TradeRef> __get_trade(size_t strategy_index) const;

    /// <summary>
    /// Evaluate a position at the current market price and allow for the placement of orders as result
    /// of the new valuation 
    /// </summary>
    /// <param name="orders">Reference to thread save vector of new orders to place</param>
    /// <param name="market_price">Current market price of the underlying asset</param>
    /// <param name="on_close">are we on close</param>
    void __evaluate(ThreadSafeVector<OrderPtr>& orders, double market_price, bool on_close);

    void close(OrderPtr const& order, std::vector<TradePtr>& trade_history);
    void adjust(AgisStrategyRef strategy, OrderPtr const& order, std::vector<TradePtr>& trade_history);


    OrderPtr generate_position_inverse();

private:
    static std::atomic<size_t> position_counter;

    /// <summary>
    /// Map between strategy id and a trade
    /// </summary>
    std::unordered_map<size_t, std::unique_ptr<Trade>> trades;
};


class Portfolio
{
public:
	Portfolio(std::string portfolio_id, double cash);

	/// <summary>
	/// Function called from the order router when an order placed to this portfolio is filled
	/// </summary>
	/// <param name="order">referance to a filled order object</param>
	void __on_order_fill(OrderPtr const& order);

    /// <summary>
    /// Get the unique index of the portfolio
    /// </summary>
    /// <returns></returns>
    size_t __get_index()const { return this->portfolio_index; }

    /// <summary>
    /// Evaluate the portfolio using the current exchange map and assets
    /// </summary>
    /// <param name="exchanges">Const ref to a Hydra's exchange map instance</param>
    void __evaluate(AgisRouter& router, ExchangeMap const& exchanges, bool on_close);

    /// <summary>
    /// Get the unique id of the portfolio
    /// </summary>
    /// <returns></returns>
    std::string __get_portfolio_id()const { return this->portfolio_id; }

    /// <summary>
    /// Does a position exist in the portfolio with a given asset index
    /// </summary>
    /// <param name="asset_index">unique asset index to search for</param>
    /// <returns></returns>
    bool position_exists(size_t asset_index) { return positions.contains(asset_index); }

    /// <summary>
    /// Get a position by asset index if it exists in the portfolio
    /// </summary>
    /// <param name="asset_index">unique asset index to search for</param>
    /// <returns></returns>
    AGIS_API std::optional<PositionRef> get_position(size_t asset_index) const;

    /// <summary>
    /// Register a new stratey to the portfolio instance
    /// </summary>
    /// <param name="strategy">A reference to an unique pointer to a AgisStrategy instance</param>
    /// <returns></returns>
    AGIS_API void register_strategy(AgisStrategyRef strategy);
    
    double get_cash() const { return this->cash; }
    double get_nlv() const { return this->nlv; }

    AGIS_API std::vector<PositionPtr> const& get_position_history() { return this->position_history; }
    AGIS_API std::vector<TradePtr> const& get_trade_history() { return this->trade_history; }

    void __reset();
    void __remember_order(OrderRef order);
    void __on_assets_expired(AgisRouter& router, ThreadSafeVector<size_t> const& ids);

private:
	/// <summary>
	/// Mutex lock on the portfolio instance
	/// </summary>
	std::mutex _mutex;

    /// <summary>
    /// Static portfolio counter used to assign unique ids on instantiation
    /// </summary>
    static std::atomic<size_t> portfolio_counter;

    /// <summary>
    /// The unique index of the portfolio
    /// </summary>
    size_t portfolio_index;

    /// <summary>
    /// The unique id of the portfolio
    /// </summary>
    std::string portfolio_id;


    void open_position(OrderPtr const& order);
    void modify_position(OrderPtr const& order);
    void close_position(OrderPtr const& order);

    /// <summary>
    /// Get a reference to existing position by asset index that we know to exist
    /// </summary>
    /// <param name="asset_index">Asset index of the position to get</param>
    /// <returns>Reference to an open position with the underlying asset</returns>
    PositionPtr& __get_position(size_t asset_index) { return positions.at(asset_index); }


    double cash;
    double starting_cash;
    double nlv;
    double unrealized_pl = 0;

    /// <summary>
    /// Map between asset index and a position
    /// </summary>
    std::unordered_map<size_t, PositionPtr> positions;

    /// <summary>
    /// Map between strategy index and ref to AgisStrategy
    /// </summary>
    std::unordered_map<size_t, AgisStrategyRef> strategies;


    std::vector<PositionPtr> position_history;
    std::vector<TradePtr> trade_history;
    std::vector<double> nlv_history;
    std::vector<double> cash_history;

};

class PortfolioMap
{
public:
	PortfolioMap() = default;

    void __evaluate(AgisRouter& router, ExchangeMap const& exchanges, bool on_close);
    void __clear() { this->portfolios.clear(); }
    void __reset();

	void __on_order_fill(OrderPtr const& order);
    void __remember_order(OrderRef order);
    void __on_assets_expired(AgisRouter& router, ThreadSafeVector<size_t> const& ids);

    void __register_portfolio(PortfolioPtr portfolio);
    void __register_strategy(AgisStrategyRef strategy);

    PortfolioPtr const& __get_portfolio(std::string const& id);
    PortfolioPtr const& __get_portfolio(size_t index) { return this->portfolios.at(index); };
    bool __portfolio_exists(std::string const& id) { return this->portfolio_map.contains(id); }

private:
	std::unordered_map<size_t, PortfolioPtr> portfolios;
    std::unordered_map<std::string, size_t> portfolio_map;
};