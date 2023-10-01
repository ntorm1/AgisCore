#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"
#include <string>   
#include <atomic>
#include "AgisEnums.h"
#include "Trade.h"
#include "Utils.h"

class Order;
class Hydra;
struct Trade;

typedef const Hydra* HydraPtr;
AGIS_API typedef std::unique_ptr<Order> OrderPtr;
AGIS_API typedef std::shared_ptr<Order> SharedOrderPtr;
AGIS_API typedef std::reference_wrapper<const OrderPtr> OrderRef;


class AGIS_API Order
{
private: 
    static std::atomic<size_t> order_counter;
    OrderType order_type;                           /// type of the order
    OrderState order_state = OrderState::PENDING;   /// state of the order
    size_t order_id;                                /// unique id of the order

    double units = 0;       /// number of units in the order
    double avg_price = 0;   /// average price the order was filled at
    std::optional<double> limit = std::nullopt;       /// limit price of the order

    long long order_create_time = 0;    /// time the order was created
    long long order_fill_time = 0;      /// time hte order was filled
    long long order_cancel_time = 0;    /// time the order was canceled

    size_t asset_index;             /// unique index of the asset
    size_t strategy_index;          /// unique id of the strategy that placed the order
    size_t portfolio_index;         /// unique id of the portflio the order was placed to

    /// <summary>
    /// An option trade exit to be given to the trade created by this order
    /// </summary>
    /// <returns></returns>
    std::optional<TradeExitPtr> exit = std::nullopt;

    /**
     * @brief On optional child order that is linked to this order is to be place on fill 
     * of the parent order
    */
    std::optional<OrderPtr> beta_hedge_order;

public:
    bool phantom_order = false;     /// is the order a phantom order (placed by benchmark strategy)
    bool force_close = false;       /// force an order to close out a position
    AssetPtr __asset = nullptr;		/// pointer to the asset the order is for

    /**
     * @brief pointer to the trade altered by this order
    */
    Trade* parent_trade = nullptr;

    Order(OrderType order_type_,
        size_t asset_index_,
        double units_,
        size_t strategy_index_,
        size_t portfolio_index_,
        std::optional<TradeExitPtr> exit = std::nullopt,
        bool phantom = false
    );

    [[nodiscard]] std::optional<double> get_limit() const { return this->limit; }
    [[nodiscard]] bool has_beta_hedge_order() const { return this->beta_hedge_order.has_value(); }
    [[nodiscard]] TradeExitPtr get_exit() const { return this->exit.value(); }
    [[nodiscard]] OrderPtr const& get_child_order_ref() const { return this->beta_hedge_order.value(); }
    [[nodiscard]] OrderPtr take_beta_hedge_order() { return std::move(this->beta_hedge_order.value()); this->beta_hedge_order = std::nullopt; }
    [[nodiscard]] OrderPtr const & get_beta_hedge_order_ref() { return this->beta_hedge_order.value(); }

    [[nodiscard]] size_t get_order_id() const { return this->order_id; }
    [[nodiscard]] size_t get_asset_index() const { return this->asset_index; }
    [[nodiscard]] size_t get_strategy_index() const { return this->strategy_index; }
    [[nodiscard]] size_t get_portfolio_index() const { return this->portfolio_index; }
    [[nodiscard]] OrderType get_order_type() const { return this->order_type; }
    [[nodiscard]] OrderState get_order_state() const { return this->order_state; }
    [[nodiscard]] std::optional<TradeExitPtr> move_exit() { return std::move(this->exit); }
    [[nodiscard]] bool has_exit() const { return this->exit.has_value(); }

    void insert_beta_hedge_order(OrderPtr child_order_) { this->beta_hedge_order = std::move(child_order_); }
    void set_limit(double limit_) { this->limit = limit_; }
    void set_create_time(long long t) { this->order_create_time = t; }
    void set_units(double units) { this->units = units; }

    [[nodiscard]] double get_average_price() const { return this->avg_price; }
    [[nodiscard]] double get_units() const { return this->units; }
    [[nodiscard]] long long get_fill_time() const { return this->order_fill_time; }


    inline bool is_filled() const { return this->order_state == OrderState::FILLED; }
    
    inline static void __reset_counter() { order_counter.store(0); }
    void set_order_create_time(long long t) { this->order_create_time = t; }
    void __set_state(OrderState state) { this->order_state = state; }
    void __set_force_close(bool force_close_) { this->force_close = force_close_; }

    OrderPtr generate_inverse_order();
    void fill(double market_price, long long fill_time);
    void cancel(long long cancel_time);
    void reject(long long reject_time);
    std::expected<rapidjson::Document, AgisException> serialize(HydraPtr hydra) const;
};
