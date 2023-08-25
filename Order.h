#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"
#include <string>
#include "Trade.h"
#include <atomic>

class Order;

AGIS_API typedef std::unique_ptr<Order> OrderPtr;
AGIS_API typedef std::shared_ptr<Order> SharedOrderPtr;
AGIS_API typedef std::reference_wrapper<const OrderPtr> OrderRef;


/// <summary>
/// An enumeration representing different order types
/// </summary>
enum class AGIS_API OrderType
{
    MARKET_ORDER,                  /**< market order*/
    LIMIT_ORDER,                   /**< limit order */
    STOP_LOSS_ORDER,               /**< stop loss order */
    TAKE_PROFIT_ORDER              /**< take profit order */
};


/// <summary>
/// brief An enumeration representing the current start of an order
/// </summary>
enum class AGIS_API OrderState
{
    PENDING,  /// order has been created but yet to be sent
    OPEN,     /// order is open on the exchange
    FILLED,   /// order has been filled by the exchange
    CANCELED, /// order has been canceled by a strategy
    REJECTED, /// order was rejected by the checker 
    CHEAT,    /// allows orders we know will fill to be filled and processed in single router call
};


/// <summary>
/// brief An enumeration representning the execution type of the order. An order can either
/// be sent by the broker as soon as it recieves it, or it can wait till the end of the open / close period.
/// </summary>
enum class AGIS_API OrderExecutionType
{
    EAGER, /// order will be placed as soon as the broker gets it
    LAZY   /// order will be placed in broker send orders sweep
};


/// <summary>
/// An enumeration representing the type of order target used for portfolio target functions.
/// Allows portfolio to easily place orders expressed in various units, not just number of shares or amount of the underlying.
/// </summary>
enum class AGIS_API OrderTargetType
{
    UNITS,              /// order target size is in units, i.e. 100 shares
    DOLLARS,            /// order target size is in dollars, i.e. $1000 at $100 a share => 10 shares
    PCT,                /// order target size is in pct of total nlv of the source portfolio
    BETA_DOLLARS,       /// order target size is in beta dollars 
    PCT_BETA_DOLLARS    /// order target size is in pct of total nlv of the source potfolio normalized by beta
};


/// <summary>
/// An enumeration representing the type of parent used for order's that require an order parent.
/// Orders like take - profit or stop - loss require a parent, either an open order or an open trade.
/// </summary>
enum class AGIS_API OrderParentType
{
    TRADE, /// parent of the order is a smart pointer to a trade
    ORDER  /// parent of the order is a smart pointer to another open order
};


/// <summary>
/// List of column names used to serialize an order
/// </summary>
static std::vector<std::string> order_column_names = {
"Order ID","Order Type","Order State","Units","Average Price","Limit",
"Order Create Time", "Order Fill Time", "Order Cancel Time", "Asset ID",
"Strategy ID","Portfolio ID"
};


class AGIS_API Order
{
protected: 
    static std::atomic<size_t> order_counter;

    OrderType order_type;                       /// type of the order
    OrderState order_state;                     /// state of the order
    size_t order_id;                            /// unique id of the order

    double units;       /// number of units in the order
    double avg_price;   /// average price the order was filled at
    std::optional<double> limit = std::nullopt;       /// limit price of the order

    long long order_create_time;    /// time the order was created
    long long order_fill_time;      /// time hte order was filled
    long long order_cancel_time;    /// time the order was canceled

    size_t asset_index;             /// unique index of the asset
    size_t strategy_index;          /// unique id of the strategy that placed the order
    size_t portfolio_index;         /// unique id of the portflio the order was placed to

    /// <summary>
    /// An option trade exit to be given to the trade created by this order
    /// </summary>
    /// <returns></returns>
    std::optional<TradeExitPtr> exit = std::nullopt;

public:
    bool force_close = false;                   /// force an order to close out a position

    typedef std::shared_ptr<Order> order_sp_t;

    Order(OrderType order_type_,
        size_t asset_index_,
        double units_,
        size_t strategy_index_,
        size_t portfolio_index_,
        std::optional<TradeExitPtr> exit = std::nullopt
    );

    [[nodiscard]] std::optional<double> get_limit() const { return this->limit; }
    [[nodiscard]] size_t get_order_id() const { return this->order_id; }
    [[nodiscard]] size_t get_asset_index() const { return this->asset_index; }
    [[nodiscard]] size_t get_strategy_index() const { return this->strategy_index; }
    [[nodiscard]] size_t get_portfolio_index() const { return this->portfolio_index; }
    [[nodiscard]] OrderType get_order_type() const { return this->order_type; }
    [[nodiscard]] OrderState get_order_state() const { return this->order_state; }
    [[nodiscard]] std::optional<TradeExitPtr> move_exit() { return std::move(this->exit); }

    void set_limit(double limit_) { this->limit = limit_; }
    void set_create_time(long long t) { this->order_create_time = t; }
    void set_units(double units) { this->units = units; }

    [[nodiscard]] double get_average_price() const { return this->avg_price; }
    [[nodiscard]] double get_units() const { return this->units; }
    [[nodiscard]] long long get_fill_time() const { return this->order_fill_time; }


    inline bool is_filled() const { return this->order_state == OrderState::FILLED; }
    
    void set_order_create_time(long long t) { this->order_create_time = t; }
    void __set_state(OrderState state) { this->order_state = state; }
    void __set_force_close(bool force_close_) { this->force_close = force_close_; }

    void fill(double market_price, long long fill_time);
    void cancel(long long cancel_time);
    void reject(long long reject_time);

};
