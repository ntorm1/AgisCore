#include "pch.h"

#undef USE_TBB
#ifdef USE_TBB
#include <tbb/concurrent_queue.h>
#include <tbb/parallel_for_each.h>
#endif


#include "AgisRouter.h"
#include "Portfolio.h"



//============================================================================
struct AgisRouterPrivate
{
#ifdef USE_TBB
    tbb::concurrent_queue<OrderPtr> channel;
#else
    ThreadSafeVector<OrderPtr> channel;
#endif
};



//============================================================================
AgisRouter::AgisRouter(ExchangeMap& exchanges_, PortfolioMap* portfolios_) :
    exchanges(exchanges_),
    portfolios(portfolios_),
    p(new AgisRouterPrivate)
{}



//============================================================================
AgisRouter::~AgisRouter() { delete p; }


//============================================================================
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

    LOCK_GUARD
    SharedOrderPtr order_ptr = std::move(order);
    this->order_history.push_back(order_ptr);
    this->portfolios->__remember_order(order_ptr);
    UNLOCK_GUARD
    
}


//============================================================================
void AgisRouter::__process() {
#ifdef USE_TBB
    if (this->p->channel.unsafe_size() == 0) { return; }
    std::for_each(
        this->p->channel.unsafe_begin(),
        this->p->channel.unsafe_end(),
        [this](OrderPtr& order) {
            processOrder(std::move(order));
        }
    );
    this->p->channel.clear();
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
