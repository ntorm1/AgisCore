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
    case OrderState::CHEAT:
        this->exchanges.__process_order(true, order);
        if (order->get_order_state() != OrderState::FILLED) { break; }
        this->portfolios->__on_order_fill(order);
        break;

    default:
        break;
    }

    this->portfolios->__remember_order(std::ref(order));

    LOCK_GUARD
        this->order_history.push_back(std::move(order));
    UNLOCK_GUARD
}