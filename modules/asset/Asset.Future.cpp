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
std::expected<bool, AgisException>
Future::set_last_trade_date(std::shared_ptr<TradingCalendar> calendar)
{
    // verify asset id is of length 7
    if (this->asset_id.length() != 7)
	{
		return std::unexpected<AgisException>("Future's asset id is not of length 7");
	}
    // get the first two chars of the asset id
    std::string first_two = this->asset_id.substr(0, 2);
    std::expected<long long, AgisException> res;
    if (first_two == "ES") {
        res = calendar->es_future_contract_to_expiry(this->asset_id);
    }
    else if (first_two == "CL") {
        res = calendar->cl_future_contract_to_expiry(this->asset_id);
    }
    else if (first_two == "ZF") {
        res = calendar->zf_futures_contract_to_first_intention(this->asset_id);
    }
    else {
		return std::unexpected<AgisException>("unrecgonized futures contract: " + this->get_asset_id());
    }
	
    if (!res) {
        return std::unexpected<AgisException>(res.error());
	}
	this->_last_trade_date = res.value();
	return true;
}


//============================================================================
struct FuturePrivate {
    FuturePrivate(Exchange* exchange)
	{
        this->_calendar = exchange->get_trading_calendar();
	}
    std::shared_ptr<TradingCalendar> _calendar;
};


//============================================================================
FutureTable::~FutureTable()
{
	delete this->p;
}


//============================================================================
FutureTable::FutureTable(
    Exchange* exchange,
    std::string contract_id) : AssetTable(exchange)
{
    this->p = new FuturePrivate(exchange);
}


//============================================================================
std::expected<bool, AgisException>
FutureTable::build()
{
    auto asset_ids = this->_exchange->get_asset_ids();
    return true;
}

} // namespace Agis
