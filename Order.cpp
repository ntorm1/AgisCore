#include "pch.h" 
#include "Order.h"
#include "Trade.h"
#include <mutex>

std::atomic<size_t> Order::order_counter(0);

Order::Order(OrderType order_type_,
    size_t asset_index_,
    double units_,
    size_t strategy_index_,
    size_t portfolio_index_,
    std::optional<TradeExitPtr> exit_
    )
{   
    this->order_state = OrderState::PENDING;
    this->order_type = order_type_;
    this->asset_index = asset_index_;
    this->units = units_;
    this->strategy_index = strategy_index_;
    this->portfolio_index = portfolio_index_;

    this->avg_price = 0.0;
    this->order_create_time = 0;
    this->order_cancel_time = 0;
    this->order_fill_time = 0;

    this->exit = std::move(exit_);

    this->order_id = order_counter++;
}

void Order::fill(double avg_price_, long long fill_time)
{
    this->avg_price = avg_price_;
    this->order_fill_time = fill_time;
    this->order_state = OrderState::FILLED;
}

void Order::cancel(long long order_cancel_time_)
{
    this->order_cancel_time = order_cancel_time_;
    this->order_state = OrderState::CANCELED;
}

void Order::reject(long long reject_time)
{
    this->order_cancel_time = reject_time;
    this->order_state = OrderState::REJECTED;
}
