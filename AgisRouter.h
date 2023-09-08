#pragma once

#include <memory>
#include <atomic>

#include "Order.h"
#include "Exchange.h"

class PortfolioMap;
struct AgisRouterPrivate;

class AgisRouter {
private:
    /// <summary>
    /// Private implementationg for the order channel
    /// </summary>
    AgisRouterPrivate* p = nullptr;

    /// <summary>
    /// Wether or not to log orders
    /// </summary>
    bool is_logging_orders = true;
    
    /// <summary>
    /// Reference to an exsiting exchange map that handles new orders
    /// </summary>
    ExchangeMap& exchanges;

    /// <summary>
    /// Reference to an exsiting portfolio map that handles filled orders
    /// </summary>
    PortfolioMap* portfolios;

    /// <summary>
    /// Container for holding past orders that were processed
    /// </summary>
    ThreadSafeVector<SharedOrderPtr> order_history;

    /// <summary>
    /// Process and incoming order either placed by a strategy or filled by an exchange and route it
    /// </summary>
    /// <param name="order"></param>
    void processOrder(OrderPtr order);

    void cheat_order(OrderPtr& order);

    void remeber_order(OrderPtr order);

    void process_beta_hedge(OrderPtr& order);

public:
    AgisRouter(
        ExchangeMap& exchanges_,
        PortfolioMap* portfolios_,
        bool is_logging_orders = true
    );
    ~AgisRouter();

    void place_order(OrderPtr order);

    void __reset();

    void __process();

    ThreadSafeVector<SharedOrderPtr> const& get_order_history() { return this->order_history; }
};