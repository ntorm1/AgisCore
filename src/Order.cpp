#include "pch.h" 
#include "Order.h"
#include "Trade.h"
#include "Hydra.h"
#include <mutex>

using namespace rapidjson;

std::atomic<size_t> Order::order_counter(0);


//============================================================================
Order::Order(OrderType order_type_,
    size_t asset_index_,
    double units_,
    size_t strategy_index_,
    size_t portfolio_index_,
    size_t broker_index_,
    std::optional<TradeExitPtr> exit_,
    bool phantom_order_
    )
{   
    this->order_type = order_type_;
    this->asset_index = asset_index_;
    this->broker_index = broker_index_;
    this->strategy_index = strategy_index_;
    this->portfolio_index = portfolio_index_;
    this->units = units_;
    this->phantom_order = phantom_order_;

    this->exit = std::move(exit_);
    this->order_id = order_counter++;
}


//============================================================================
std::expected<rapidjson::Document, AgisException> Order::serialize(HydraPtr hydra) const
{
    Document order(kObjectType);
    
    order.AddMember("Order ID", order_id, order.GetAllocator());
    order.AddMember("Order Type", Value(OrderTypeToString(order_type), order.GetAllocator()).Move(), order.GetAllocator());
    order.AddMember("Order State", Value(OrderStateToString(order_state), order.GetAllocator()).Move(), order.GetAllocator());
    order.AddMember("Units", units, order.GetAllocator());
    order.AddMember("Average Price", avg_price, order.GetAllocator());
    if (limit.has_value()) {
        order.AddMember("Limit", limit.value(), order.GetAllocator());
    }
    else {
        order.AddMember("Limit", 0.0f, order.GetAllocator());
    }

    order.AddMember("Order Create Time", order_create_time, order.GetAllocator());
    order.AddMember("Order Fill Time", order_fill_time, order.GetAllocator());
    order.AddMember("Order Cancel Time", order_cancel_time, order.GetAllocator());

    Value asset_id, strategy_id, portfolio_id;
    try {
        asset_id.SetString(hydra->asset_index_to_id(asset_index).unwrap().c_str(), order.GetAllocator());
        strategy_id.SetString(hydra->strategy_index_to_id(strategy_index).unwrap().c_str(), order.GetAllocator());
        portfolio_id.SetString(hydra->portfolio_index_to_id(portfolio_index).unwrap().c_str(), order.GetAllocator());
    }
    catch (AgisException& e) {
        return std::unexpected<AgisException> {e.what()};
    }

    order.AddMember("Asset Identifier", asset_id, order.GetAllocator());
    order.AddMember("Strategy Identifier", strategy_id, order.GetAllocator());
    order.AddMember("Portfolio Identifier", portfolio_id, order.GetAllocator());

    return order;
}


//============================================================================
OrderPtr Order::generate_inverse_order()
{
    auto order = std::make_unique<Order>(
        OrderType::MARKET_ORDER,
        this->asset_index,
        -1 * this->units,
        this->strategy_index,
        this->portfolio_index,
        this->broker_index
    );
    order->__asset = this->__asset;
    return std::move(order);
}

//============================================================================
void Order::fill(double avg_price_, long long fill_time)
{
    this->avg_price = avg_price_;
    this->order_fill_time = fill_time;
    this->order_state = OrderState::FILLED;
}


//============================================================================
void Order::cancel(long long order_cancel_time_)
{
    this->order_cancel_time = order_cancel_time_;
    this->order_state = OrderState::CANCELED;
}


//============================================================================
void Order::reject(long long reject_time)
{
    this->order_cancel_time = reject_time;
    this->order_state = OrderState::REJECTED;
}