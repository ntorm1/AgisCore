#pragma once

#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "pch.h"
#include <functional>
#include <limits>

#include <ankerl/unordered_dense.h>
#include "AgisPointers.h"
#include "AgisRouter.h"
#include "AgisAnalysis.h"
#include "Exchange.h"
#include "Order.h"
#include "Trade.h"

class Portfolio;
class PortfolioMap;
class AgisStrategy;
class AgisStrategyMap;
struct Position;

AGIS_API typedef std::unique_ptr<AgisStrategy> AgisStrategyPtr;
AGIS_API typedef std::reference_wrapper<const AgisStrategyPtr> AgisStrategyRef;

AGIS_API typedef std::shared_ptr<Portfolio> PortfolioPtr;
AGIS_API typedef std::reference_wrapper<const PortfolioPtr> PortfolioRef;
AGIS_API typedef std::unique_ptr<Position> PositionPtr;
AGIS_API typedef std::shared_ptr<Position> SharedPositionPtr;
AGIS_API typedef std::reference_wrapper<const PositionPtr> PositionRef;


/// <summary>
/// Vector of column names used to serialize the position data
/// </summary>
static std::vector<std::string> position_column_names = {
    "Position ID","Asset ID","Portfolio ID","Units","Average Price",
    "Position Open Time","Position Close Time","Close Price","Last Price", "NLV",
    "Unrealized PL", "Realized PL", "Bars Held"
};



struct Frictions
{
    /// <summary>
    /// Flat fee associated with an order i.e. $5.00
    /// </summary>
    std::optional<double> flat_commisions;

    /// <summary>
    /// A per units commision associated with an order. I.e. $0.01 per share
    /// </summary>
    std::optional<double> per_unit_commisions;

    /// <summary>
    /// A percentage of the order value that is subtracted. I.e. 0.01% of order value
    /// </summary>
    std::optional<double> slippage;

    /// <summary>
    /// Fristions constructor
    /// </summary>
    /// <param name="flat_commisions">Flat fee associated with an order i.e. $5.00</param>
    /// <param name="per_unit_commisions">A per units commision associated with an order. I.e. $0.01 per share</param>
    /// <param name="slippage">A percentage of the order value that is subtracted. I.e. 0.01% of order value</param>
    Frictions(std::optional<double> flat_commisions, std::optional<double> per_unit_commisions, std::optional<double> slippage) :
		flat_commisions(flat_commisions), per_unit_commisions(per_unit_commisions), slippage(slippage) {}

    /// <summary>
    /// Calculate the frictions associated with an order
    /// </summary>
    /// <param name="order"></param>
    /// <returns></returns>
    double calculate_frictions(OrderPtr const& order);

    double total_flat_commisions = 0;
    double total_per_unit_commisions = 0;
    double total_slippage = 0;
};

struct Position
{
    AssetPtr __asset;
    size_t position_id;
    size_t asset_index;
    size_t portfolio_id;

    double close_price = 0;
    double average_price = 0;
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

    Position(MAgisStrategyRef strategy, OrderPtr const& order);

    std::optional<TradeRef> __get_trade(size_t strategy_index) const;
    size_t __get_trade_count() const { return this->trades.size(); }
    size_t __trade_exits(size_t strategy_index) const { return this->trades.count(strategy_index) > 0; }

    /// <summary>
    /// Evaluate a position at the current market price and allow for the placement of orders as result
    /// of the new valuation 
    /// </summary>
    /// <param name="orders">Reference to thread save vector of new orders to place</param>
    /// <param name="market_price">Current market price of the underlying asset</param>
    /// <param name="on_close">are we on close</param>
    void __evaluate(ThreadSafeVector<OrderPtr>& orders, bool on_close, bool is_reprice);

    void close(OrderPtr const& order, std::vector<std::shared_ptr<Trade>>& trade_history);
    void adjust(MAgisStrategyRef strategy, OrderPtr const& order, std::vector<SharedTradePtr>& trade_history);

    [[nodiscard]] double get_nlv() const { return this->nlv; }
    [[nodiscard]] double get_unrealized_pl() const { return this->unrealized_pl; }
    [[nodiscard]] double get_realized_pl() const { return this->realized_pl; }
    [[nodiscard]] double get_units() const { return this->units; }


    OrderPtr generate_position_inverse();

private:
    static std::atomic<size_t> position_counter;

    /// <summary>
    /// Map between strategy id and a trade
    /// </summary>
    ankerl::unordered_dense::map<size_t, SharedTradePtr> trades;  
};


class Portfolio
{
    friend class PortfolioMap;
    friend class PortfolioStats;
public:
    AGIS_API Portfolio(std::string const& portfolio_id, double cash);

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
    /// <param name="router">Reference to the order router</param>
    /// <param name="exchanges">Const ref to a Hydra's exchange map instance</param>
    /// <param name="on_close">Are we on close</param>
    /// <param name="is_reprice">Is this a reprice, i.e. just evaluate the portfolio at current prices</param>
    void __evaluate(AgisRouter& router, ExchangeMap const& exchanges, bool on_close, bool is_reprice = false);

    /// <summary>
    /// Get the unique id of the portfolio
    /// </summary>
    /// <returns></returns>
    std::string const& __get_portfolio_id()const { return this->portfolio_id; }

    /// <summary>
    /// Does a position exist in the portfolio with a given asset index
    /// </summary>
    /// <param name="asset_index">unique asset index to search for</param>
    /// <returns></returns>
    AGIS_API inline bool position_exists(size_t asset_index) { return positions.count(asset_index) > 0; }

    /// <summary>
    /// Get a position by asset index if it exists in the portfolio
    /// </summary>
    /// <param name="asset_index">unique asset index to search for</param>
    /// <returns></returns>
    AGIS_API std::optional<PositionRef> get_position(size_t asset_index) const;

    /// <summary>
    /// Get a trade by asset index and portfolio id if it exists
    /// </summary>
    /// <param name="asset_index">unique index of the asset</param>
    /// <param name="strategy_id">unique id of the strategy</param>
    /// <returns></returns>
    AGIS_API std::optional<TradeRef> get_trade(size_t asset_index, std::string const& strategy_id);

    AGIS_API std::vector<size_t> get_strategy_positions(size_t strategy_index) const;
    AGIS_API std::vector<std::string> get_strategy_ids() const;

    /// <summary>
    /// Register a new stratey to the portfolio instance
    /// </summary>
    /// <param name="strategy">A reference to an unique pointer to a AgisStrategy instance</param>
    /// <returns></returns>
    AGIS_API void register_strategy(MAgisStrategyRef strategy);


    AGIS_API PortfolioStats const& get_stats() const { return this->stats; }
    double inline get_cash() const { return this->cash; }
    double inline get_nlv() const { return this->nlv; }
    double inline get_unrealized_pl() const { return this->unrealized_pl; }

    AGIS_API inline std::vector<SharedPositionPtr> const& get_position_history() { return this->position_history; }
    AGIS_API inline std::vector<SharedTradePtr> const& get_trade_history() { return this->trade_history; }
    AGIS_API inline std::vector<double> const& get_nlv_history() { return this->nlv_history; }
    AGIS_API inline std::vector<double> const& get_cash_history() { return this->cash_history; }

    json to_json() const;
    void restore(json const& strategies);
    void __reset();
    void __remove_strategy(size_t index);
    inline bool __strategy_exists(size_t index) { return this->strategies.contains(index); }
    static void __reset_counter() { Portfolio::portfolio_counter = 0; }
    void __remember_order(SharedOrderPtr order);
    void __on_assets_expired(AgisRouter& router, ThreadSafeVector<size_t> const& ids);


protected:
    /// <summary>
    /// Map between strategy index and ref to AgisStrategy
    /// </summary>
    ankerl::unordered_dense::map<size_t, MAgisStrategyRef> strategies;
    ankerl::unordered_dense::map<std::string, size_t> strategy_ids;

    std::vector<double> nlv_history;
    std::vector<double> cash_history;

    /// <summary>
    /// Mutex lock on the portfolio instance
    /// </summary>
    std::mutex _mutex;

private:
    void open_position(OrderPtr const& order);
    void modify_position(OrderPtr const& order);
    void close_position(OrderPtr const& order);

    /// <summary>
    /// Get a reference to existing position by asset index that we know to exist
    /// </summary>
    /// <param name="asset_index">Asset index of the position to get</param>
    /// <returns>Reference to an open position with the underlying asset</returns>
    PositionPtr& __get_position(size_t asset_index) { return positions.at(asset_index); }

    /// <summary>
    /// When new trades are pushed to trade history, update their respective strategy
    /// </summary>
    /// <param name="start_index">index of first trade to copy</param>
    void __on_trade_closed(size_t start_index);


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

    double cash;
    double starting_cash;
    double nlv;
    double unrealized_pl = 0;

    /// <summary>
    /// Map between asset index and a position
    /// </summary>
    ankerl::unordered_dense::map<size_t, PositionPtr> positions;

    std::optional<Frictions> frictions;
    PortfolioStats stats;
    std::vector<SharedPositionPtr> position_history;
    std::vector<SharedTradePtr> trade_history;
};

class PortfolioMap
{
public:
	PortfolioMap() = default;

    void __evaluate(AgisRouter& router, ExchangeMap const& exchanges, bool on_close, bool is_reprice = false);
    void __clear();
    void __reset();
    void __build(size_t size);

	void __on_order_fill(OrderPtr const& order);
    void __remember_order(SharedOrderPtr order);
    void __on_assets_expired(AgisRouter& router, ThreadSafeVector<size_t> const& ids);

    void __register_portfolio(PortfolioPtr portfolio);
    void __remove_portfolio(std::string const& portfolio_id);
    void __remove_strategy(size_t strategy_index);
    void __register_strategy(MAgisStrategyRef strategy);
    void __reload_strategies(AgisStrategyMap* strategies);

    AgisResult<std::string> __get_portfolio_id(size_t const& index) const;
    size_t const __get_portfolio_index(std::string const& id) const { return this->portfolio_map.at(id); }
    PortfolioPtr const __get_portfolio(std::string const& id) const;
    PortfolioPtr const __get_portfolio(size_t index) const { return this->portfolios.at(index); };
    bool __portfolio_exists(std::string const& id) const { return this->portfolio_map.count(id) > 0; }

    AGIS_API PortfolioRef get_portfolio(std::string const& id) const;
    AGIS_API std::vector<std::string> get_portfolio_ids() const;
    AGIS_API json to_json() const;
    AGIS_API void restore(json const& j);

private:
    ankerl::unordered_dense::map<size_t, PortfolioPtr> portfolios;
    ankerl::unordered_dense::map<std::string, size_t> portfolio_map;
};