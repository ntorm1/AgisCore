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

namespace Agis {
    class Broker;
}

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

    double units = 0;           /// number of units in the order
    double avg_price = 0;       /// average price the order was filled at
    double cash_impact = 0;     /// cash impact of the order set by the broker on fill
    double margin_impact = 0;   /// margin required to put on the order
    std::optional<double> limit = std::nullopt;       /// limit price of the order

    long long order_create_time = 0;    /// time the order was created
    long long order_fill_time = 0;      /// time hte order was filled
    long long order_cancel_time = 0;    /// time the order was canceled

    size_t asset_index;             /// unique index of the asset
    size_t strategy_index;          /// unique id of the strategy that placed the order
    size_t portfolio_index;         /// unique id of the portflio the order was placed to
    size_t broker_index; 		    /// unique id of the broker the order was placed to

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
    Broker* __broker = nullptr;		/// pointer to the broker the order was placed on 

    /**
     * @brief pointer to the trade altered by this order
    */
    Trade* parent_trade = nullptr;

    Order(OrderType order_type_,
        size_t asset_index_,
        double units_,
        size_t strategy_index_,
        size_t portfolio_index_,
        size_t broker_index_,
        std::optional<TradeExitPtr> exit = std::nullopt,
        bool phantom = false
    );

    [[nodiscard]] inline std::optional<double> get_limit() const noexcept{ return this->limit; }
    [[nodiscard]] inline bool has_beta_hedge_order() const noexcept { return this->beta_hedge_order.has_value(); }
    [[nodiscard]] inline TradeExitPtr get_exit() const noexcept { return this->exit.value(); }
    [[nodiscard]] inline OrderPtr const& get_child_order_ref() const noexcept { return this->beta_hedge_order.value(); }
    [[nodiscard]] inline OrderPtr take_beta_hedge_order() noexcept { return std::move(this->beta_hedge_order.value()); this->beta_hedge_order = std::nullopt; }
    [[nodiscard]] inline OrderPtr const & get_beta_hedge_order_ref() noexcept { return this->beta_hedge_order.value(); }

    [[nodiscard]] inline size_t get_order_id() const noexcept { return this->order_id; }
    [[nodiscard]] inline size_t get_asset_index() const noexcept { return this->asset_index; }
    [[nodiscard]] inline size_t get_strategy_index() const noexcept { return this->strategy_index; }
    [[nodiscard]] inline size_t get_broker_index() const noexcept { return this->broker_index; }
    [[nodiscard]] inline size_t get_portfolio_index() const noexcept { return this->portfolio_index; }
    [[nodiscard]] inline OrderType get_order_type() const noexcept { return this->order_type; }
    [[nodiscard]] inline OrderState get_order_state() const noexcept { return this->order_state; }
    [[nodiscard]] inline std::optional<TradeExitPtr> move_exit() noexcept { return std::move(this->exit); }
    [[nodiscard]] bool has_exit() const noexcept { return this->exit.has_value(); }

    void insert_beta_hedge_order(OrderPtr child_order_) { this->beta_hedge_order = std::move(child_order_); }
    void set_limit(double limit_) { this->limit = limit_; }
    void set_create_time(long long t) { this->order_create_time = t; }
    void set_units(double units) { this->units = units; }
    void set_cash_impact(double cash_impact) noexcept { this->cash_impact = cash_impact; }
    void set_margin_impact(double margin_impact) noexcept { this->margin_impact = margin_impact; }

[[nodiscard]] double get_margin_impact() const noexcept { return this->margin_impact; }
    [[nodiscard]] double get_cash_impact() const noexcept { return this->cash_impact; }
    [[nodiscard]] double get_average_price() const noexcept { return this->avg_price; }
    [[nodiscard]] double get_units() const noexcept { return this->units; }
    [[nodiscard]] long long get_fill_time() const noexcept { return this->order_fill_time; }


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
