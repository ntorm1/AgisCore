#pragma once

#include <memory>
#include <thread>
#include <mutex>
#include <atomic>

#include "Order.h"
#include "Exchange.h"

class PortfolioMap;
struct AgisRouterPrivate;

class AgisRouter {
private:
    AgisRouterPrivate* p = nullptr;
    std::mutex _mutex;
    
    /// <summary>
    /// Reference to an exsiting exchange map that handles new orders
    /// </summary>
    ExchangeMap& exchanges;

    /// <summary>
    /// Reference to an exsiting portfolio map that handles filled orders
    /// </summary>
    PortfolioMap* portfolios;

    std::vector<SharedOrderPtr> order_history;

    void processOrder(OrderPtr order);

public:
    AgisRouter(ExchangeMap& exchanges_, PortfolioMap* portfolios_);
    ~AgisRouter();

    void place_order(OrderPtr order);

    void __reset();

    void __process();

    std::vector<SharedOrderPtr> const& get_order_history() { return this->order_history; }
};