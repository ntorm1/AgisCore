#pragma once
#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include <memory>
#include <atomic>

#include "Order.h"

namespace Agis {
    class BrokerMap;
};

class PortfolioMap;
class ExchangeMap;
struct AgisRouterPrivate;

class AgisRouter {
protected:
    /// <summary>
    /// Private implementationg for the order channel
    /// </summary>
    AgisRouterPrivate* p = nullptr;

    /// <summary>
    /// Wether or not to log orders
    /// </summary>
    bool log_orders = true;
    
    /// <summary>
    /// Reference to an exsiting exchange map that handles new orders
    /// </summary>
    ExchangeMap* exchanges;

    /**
     * @brief Pointer to an exsiting broker map that handles new orders
    */
    Agis::BrokerMap* brokers;

    /// <summary>
    /// Reference to an exsiting portfolio map that handles filled orders
    /// </summary>
    PortfolioMap* portfolios;

    /// <summary>
    /// Container for holding past orders that were processed
    /// </summary>
    ThreadSafeVector<SharedOrderPtr> order_history;

    void processOrder(OrderPtr order);
    void cheat_order(OrderPtr& order);
    void remeber_order(OrderPtr order);
    void process_child_orders(OrderPtr& order) noexcept;
    void process_beta_hedge(OrderPtr& order);

public:
    AgisRouter(
        ExchangeMap* exchanges_,
        Agis::BrokerMap* brokers_,
        PortfolioMap* portfolios_,
        bool is_logging_orders = true
    );
    ~AgisRouter();

    void place_order(OrderPtr order);

    void __reset();

    AGIS_API void __process();

    ThreadSafeVector<SharedOrderPtr> const& get_order_history() { return this->order_history; }
};