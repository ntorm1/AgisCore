#include "pch.h"

#include "AgisRouter.h"
#include "Portfolio.h"


void AgisRouter::processOrder(OrderPtr order) {
    if (!order) { return; }
    switch (order->get_order_state())
    {
    case OrderState::PENDING:
        this->exchanges.__place_order(std::move(order));
        return;
    case OrderState::FILLED:
        this->portfolios->__on_order_fill(order);
        break;
    default:
        break;
    }

    LOCK_GUARD
        this->order_history.push_back(std::move(order));
    UNLOCK_GUARD
}