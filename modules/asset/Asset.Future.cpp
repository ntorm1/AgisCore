module;
#pragma once
#define _SILENCE_CXX23_DENORM_DEPRECATION_WARNING

#include <string>
#include <memory>

#include "Exchange.h"

module Asset:Future;

import :Base;
import TradingCalendar;

namespace Agis
{


//============================================================================
struct FuturePrivate {
    FuturePrivate(std::shared_ptr<Exchange> exchange):
        _exchange(exchange)
	{
        this->_calendar = this->_exchange->get_trading_calendar();
	}
    std::shared_ptr<TradingCalendar> _calendar;
    std::shared_ptr<Exchange> _exchange;
};


//============================================================================
FutureTable::~FutureTable()
{
	delete this->p;
}


//============================================================================
FutureTable::FutureTable(
    std::shared_ptr<Exchange> exchange,
    std::string contract_id)
{
    this->p = new FuturePrivate(exchange);
    //if(!this->p->_calendar)
	//{
	//	throw std::runtime_error("Exchange does not have a trading calendar");
	//}
}

} // namespace Agis
