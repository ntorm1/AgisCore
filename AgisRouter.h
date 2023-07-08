#pragma once

#include <tbb/concurrent_queue.h>
#include <tbb/parallel_for_each.h>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>

#include "Order.h"
#include "Exchange.h"
#include "Portfolio.h"


class AgisRouter {
private:
    tbb::concurrent_queue<OrderPtr> channel;

    std::vector<std::thread> listenerThreads_;
    std::atomic<bool> stopListening_;
    std::mutex _mutex;
    
    /// <summary>
    /// Reference to an exsiting exchange map that handles new orders
    /// </summary>
    ExchangeMap& exchanges;

    /// <summary>
    /// Reference to an exsiting portfolio map that handles filled orders
    /// </summary>
    PortfolioMap& portfolios;

    std::vector<OrderPtr> order_history;



    void processOrder(OrderPtr order) {
        if (!order) { return; }
        switch (order->get_order_state())
        {
        case OrderState::PENDING:
            this->exchanges.__place_order(std::move(order));
            return;
        case OrderState::FILLED:
            this->portfolios.__on_order_fill(order);
            break;
        default:
            break;
        }

        LOCK_GUARD
        this->order_history.push_back(std::move(order));
        UNLOCK_GUARD
    }

public:
    AgisRouter(ExchangeMap& exchanges_, PortfolioMap& portfolios_) :
        exchanges(exchanges_),
        portfolios(portfolios_)
    {}

    void place_order(OrderPtr order) {
        channel.push(std::move(order));
    }

    void __process() {
        tbb::parallel_for_each(
            this->channel.unsafe_begin(),
            this->channel.unsafe_end(),
            [this](OrderPtr& order) {
                processOrder(std::move(order));
            }
        );
    }


    std::vector<OrderPtr> const& get_order_history() { return this->order_history; }
};