#pragma once
#include "pch.h"
#include <atomic>

#include "AgisPointers.h"
#include "Order.h"


class Portfolio;
struct Trade;
struct Position;

typedef std::unique_ptr<Portfolio> PortfolioPtr;
typedef std::unique_ptr<Position> PositionPtr;
typedef std::unique_ptr<Trade> TradePtr;


struct Trade {
    double units;
    double average_price;
    double close_price;
    double last_price;

    double unrealized_pl;
    double realized_pl;
    double nlv;

    long long trade_open_time;
    long long trade_close_time;
    size_t bars_held;

    size_t trade_id;
    size_t asset_id;
    size_t strategy_id;

    Trade(OrderPtr const& order);

    void close(OrderPtr const& filled_order);
    void increase(OrderPtr const& filled_order);
    void reduce(OrderPtr const& filled_order);
    void adjust(OrderPtr const& filled_order);;

private:
    static std::atomic<size_t> trade_counter;
};

struct Position
{
    
    size_t position_id;
    size_t asset_id;

    double close_price = 0;
    double average_price;
    double last_price = 0;

    double unrealized_pl = 0;
    double realized_pl = 0;
    double nlv = 0;
    double units = 0;

    long long position_open_time;
    long long position_close_time = 0;

    size_t bars_held = 0;

    Position(OrderPtr const& order);

    Trade* __get_trade(size_t strategy_index);

    void close(OrderPtr const& order, std::vector<TradePtr>& trade_history);
    void adjust(OrderPtr const& order, std::vector<TradePtr>& trade_history);

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
    /// Get the unique id of the portfolio
    /// </summary>
    /// <returns></returns>
    std::string __get_portfolio_id()const { return this->portfolio_id; }


    bool position_exists(size_t asset_index) { return positions.contains(asset_index); }

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

    /// <summary>
    /// Map between asset index and a position
    /// </summary>
    std::unordered_map<size_t, PositionPtr> positions;


    std::vector<PositionPtr> position_history;
    std::vector<TradePtr> trade_history;

};

class PortfolioMap
{
public:
	PortfolioMap() = default;

    void __clear() { this->portfolios.clear(); }
	void __on_order_fill(OrderPtr const& order);
    void __register_portfolio(PortfolioPtr portfolio);

private:
	std::unordered_map<size_t, PortfolioPtr> portfolios;
    std::unordered_map<std::string, size_t> portfolio_map;
};