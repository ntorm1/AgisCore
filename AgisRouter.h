#pragma once

#include <tbb/concurrent_queue.h>
#include <tbb/parallel_for_each.h>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>

#include "Order.h"
#include "Exchange.h"

class PortfolioMap;


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
    PortfolioMap* portfolios;

    std::vector<OrderPtr> order_history;

    void processOrder(OrderPtr order);

public:
    AgisRouter(ExchangeMap& exchanges_, PortfolioMap* portfolios_) :
        exchanges(exchanges_),
        portfolios(portfolios_)
    {}

    void place_order(OrderPtr order) {
        channel.push(std::move(order));
    }

    void __reset() {
        this->channel.clear();
        this->order_history.clear();
    }

    void __process() {
        if (this->channel.unsafe_size() == 0) { return; }
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