#include "pch.h"

//#undef USE_TBB
#ifdef USE_TBB
#include <tbb/concurrent_queue.h>
#include <tbb/parallel_for_each.h>
#include <tbb/parallel_for.h>
#include <tbb/task_group.h>

constexpr size_t num_threads = 1;
#endif

#include "AgisRouter.h"
#include "Portfolio.h"
#include "Exchange.h"


#include "Broker/Broker.Base.h"

using namespace Agis;

//============================================================================
struct AgisRouterPrivate
{
#ifdef USE_TBB
    
    tbb::concurrent_queue<OrderPtr> channel;
    tbb::task_group tg;
    tbb::blocked_range<size_t> tbb_block{ 0, num_threads };

    AgisRouterPrivate() {}

#else
    ThreadSafeVector<OrderPtr> channel;
#endif
};


//============================================================================
AgisRouter::AgisRouter(
    ExchangeMap* exchanges_,
    BrokerMap* brokers_,
    PortfolioMap* portfolios_,
    bool is_logging_orders_
) :
    exchanges(exchanges_),
    brokers(brokers_),
    portfolios(portfolios_),
    p(new AgisRouterPrivate)
{
    this->log_orders = is_logging_orders_;
}


//============================================================================
AgisRouter::~AgisRouter() { delete p; }


//============================================================================
void AgisRouter::remeber_order(OrderPtr order)
{
    SharedOrderPtr order_ptr = std::move(order);
    this->order_history.push_back(order_ptr);
    this->portfolios->__remember_order(order_ptr);
}


//============================================================================
void AgisRouter::process_beta_hedge(OrderPtr& order)
{
    auto child_order = order->take_beta_hedge_order();
    child_order->__set_state(OrderState::CHEAT);
    this->cheat_order(child_order);

    // adjut the trades beta hedge partition 
    auto asset_index = child_order->get_asset_index();
    if (!order->parent_trade->partition_exists(asset_index)) {
        auto partition = std::make_unique<TradePartition>(
            order->parent_trade,
            child_order->parent_trade,
            child_order->get_units()
        );
        order->parent_trade->take_partition(std::move(partition));
    }
    else {
        auto partition = order->parent_trade->get_child_partition(child_order->get_asset_index());
        partition->child_trade_units += child_order->get_units();
    }
    this->remeber_order(std::move(child_order));
}


//============================================================================
void
AgisRouter::process_child_orders(OrderPtr& order) noexcept
{
    auto& child_orders = order->get_child_orders();
    for (auto& child_order : child_orders) {
        this->brokers->__validate_order(child_order);
        if (!child_order) continue;
        if (child_order->get_order_state() == OrderState::REJECTED) {
            this->remeber_order(std::move(child_order));
            continue;
        }
		child_order->__set_state(OrderState::CHEAT);
		this->cheat_order(child_order);
		this->remeber_order(std::move(child_order));
	}
}

//============================================================================
void AgisRouter::processOrder(OrderPtr order) {
    if (!order) { return; }

    // get reference wrapper to the order
    std::reference_wrapper<OrderPtr> order_ref = order;

    switch (order->get_order_state())
    {
    case OrderState::REJECTED:
        // order was rejected by the exchange or the strategy order validator and is pushed to the history
        break;
    case OrderState::PENDING:
        // order has been placed by a strategy and is routed to the correct exchange
        this->brokers->__validate_order(order_ref);
        if(!order) break;
        if(order->get_order_state() == OrderState::REJECTED) break;
        this->exchanges->__place_order(std::move(order_ref.get()));
        return;
    case OrderState::FILLED: {
        // order has been filled by the exchange and is routed to the portfolio
        this->brokers->__on_order_fill(order_ref);
        this->portfolios->__on_order_fill(order_ref);
        // allow for beta hedge to be processed in the same step
        if (order->has_beta_hedge_order()) {
            this->process_beta_hedge(order);
        }   
        // allow for any child order to be processed in the same step
        if (order->has_child_orders()) {
            this->process_child_orders(order);
        }
        break;
    }
    case OrderState::CHEAT:
        // order was placed using the cheat method that allows the router to process
        // the order on the exchange and route the order to the portfolio in the same step
        this->cheat_order(order);
        break;

    default:
        break;
    }

    if (!log_orders) return;
    if (!order) return;
    this->remeber_order(std::move(order));
}

//============================================================================
void AgisRouter::cheat_order(OrderPtr& order)
{
    this->exchanges->__process_order(true, order);
    if (order->get_order_state() != OrderState::FILLED) return;
    this->brokers->__on_order_fill(order);
    this->portfolios->__on_order_fill(order);
    if (order->has_child_orders()) {
		this->process_child_orders(order);
	}
}


//============================================================================
void AgisRouter::__process() {
#ifdef USE_TBB
    if (!this->p->channel.unsafe_size()) return;
    p->tg.run([&] {
        tbb::parallel_for(
            p->tbb_block,
            [this](const tbb::blocked_range<size_t>& r) {
                while (true) {
                    OrderPtr order = nullptr;
                    auto res = this->p->channel.try_pop(order);
                    if (!res || !order) break;
                    this->processOrder(std::move(order));
                }
            }
        );
        }
    );
    p->tg.wait();

#else
    if (this->p->channel.size() == 0) { return; }
	    std::for_each(
		    this->p->channel.begin(),
		    this->p->channel.end(),
		    [this](OrderPtr& order) {
			    processOrder(std::move(order));
		    }
	);
	this->p->channel.clear();
#endif
}


//============================================================================
void AgisRouter::place_order(OrderPtr order) {
#ifdef USE_TBB
    p->channel.push(std::move(order));
#else
    p->channel.push_back(std::move(order));
#endif
}


//============================================================================
void AgisRouter::__reset() {
    this->p->channel.clear();
    this->order_history.clear();
}
