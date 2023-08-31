#include "pch.h" 
#include "Order.h"
#include "Trade.h"
#include "Hydra.h"
#include <mutex>

std::atomic<size_t> Order::order_counter(0);


//============================================================================
Order::Order(OrderType order_type_,
    size_t asset_index_,
    double units_,
    size_t strategy_index_,
    size_t portfolio_index_,
    std::optional<TradeExitPtr> exit_,
    bool phantom_order_
    )
{   
    this->order_type = order_type_;
    this->asset_index = asset_index_;
    this->units = units_;
    this->strategy_index = strategy_index_;
    this->portfolio_index = portfolio_index_;
    this->phantom_order = phantom_order_;

    this->exit = std::move(exit_);
    this->order_id = order_counter++;
}


AgisResult<json> Order::serialize(json& order, HydraPtr hydra) const
{
    if (order.size()) { order.clear(); }
    
    order["Order ID"] = this->order_id;
    order["Order Type"] = this->order_type;
    order["Order State"] = this->order_state;
    order["Units"] = this->units;
	order["Average Price"] = this->avg_price;
    order["Limit"] = this->limit.value_or(0.0f);
    order["Order Create Time"] = this->order_create_time;   
    order["Order Fill Time"] = this->order_fill_time;
    order["Order Cancel Time"] = this->order_cancel_time;
     
    AGIS_ASSIGN_OR_RETURN(order["Asset Identifier"], hydra->asset_index_to_id(this->asset_index), std::string, json);
    AGIS_ASSIGN_OR_RETURN(order["Strategy Identifier"], hydra->strategy_index_to_id(this->strategy_index), std::string, json);
    AGIS_ASSIGN_OR_RETURN(order["Portfolio Identifier"], hydra->portfolio_index_to_id(this->portfolio_index), std::string, json);

    return AgisResult<json>(order);
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