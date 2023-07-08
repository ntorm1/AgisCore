#pragma once

#include <tbb/concurrent_queue.h>
#include <memory>
#include <thread>
#include <atomic>

#include "Order.h"
#include "Exchange.h"
#include "Portfolio.h"


class AgisRouter {
private:
    tbb::concurrent_queue<OrderPtr> channel;

    std::vector<std::thread> listenerThreads_;
    std::atomic<bool> stopListening_;
    
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
        this->order_history.push_back(std::move(order));
    }

public:
    AgisRouter(ExchangeMap& exchanges_, PortfolioMap& portfolios_) :
        exchanges(exchanges_),
        portfolios(portfolios_)
    {}

    void place_order(OrderPtr order) {
        channel.push(std::move(order));
    }

    void __start_listening(int thread_num) {
        for (int i = 0; i < thread_num; ++i) {
            listenerThreads_.emplace_back([this]() {
                while (!stopListening_) {
                    OrderPtr order;
                    if (channel.try_pop(order)) {
                        processOrder(std::move(order));
                    }
                    else {
                        std::this_thread::yield(); // Allow other threads to run
                    }
                }
                });
        }
    }

    void __stop_listening() {
        stopListening_ = true;

        for (auto& thread : listenerThreads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        listenerThreads_.clear();
    }

    std::vector<OrderPtr> const& get_order_history() { return this->order_history; }
};