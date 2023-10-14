#pragma once
#define _SILENCE_CXX23_DENORM_DEPRECATION_WARNING
#define _SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING
#include <string>
#include <memory>
#include <unordered_map>
#include "Exchange.h"

#include "Asset/Asset.Future.h"
#include "Time/TradingCalendar.h"

namespace Agis
{

//============================================================================
std::expected<bool, AgisException> Future::set_future_code()
{
	char month_code = this->asset_id[2];
	static const std::unordered_map<char, FutureMonthCode> monthCodeMap = {
		{'F', FutureMonthCode::F},
		{'G', FutureMonthCode::G},
		{'H', FutureMonthCode::H},
		{'J', FutureMonthCode::J},
		{'K', FutureMonthCode::K},
		{'M', FutureMonthCode::M},
		{'N', FutureMonthCode::N},
		{'Q', FutureMonthCode::Q},
		{'U', FutureMonthCode::U},
		{'V', FutureMonthCode::V},
		{'X', FutureMonthCode::X},
		{'Z', FutureMonthCode::Z}
	};

	auto it = monthCodeMap.find(month_code);
	if (it != monthCodeMap.end()) {
		this->_month_code = it->second;
	}
	else {
		return std::unexpected<AgisException>("Invalid month code");
	}
	return true;
}


//============================================================================
std::expected<bool, AgisException> Future::set_future_parent_contract()
{
	std::string contract_id = this->asset_id.substr(0, 2);
	if (contract_id == "ES") {
		this->_parent_contract = FutureParentContract::ES;
	}
	else if (contract_id == "CL") {
		this->_parent_contract = FutureParentContract::CL;
	}
	else if (contract_id == "ZF") {
		this->_parent_contract = FutureParentContract::ZF;
	}
	else {
		return std::unexpected<AgisException>("Invalid future code");
	}
	return true;
}


//============================================================================
[[nodiscard]] std::expected<bool, AgisException> Future::__build() noexcept
{
	if (this->asset_id.length() != 7)
	{
		return std::unexpected<AgisException>("invalid futures contract: " + this->asset_id);
	}
	return this->set_future_code()
		.and_then([&](bool const& b) {return this->set_future_parent_contract(); });
}



//============================================================================
std::expected<bool, AgisException>
Future::set_last_trade_date(std::shared_ptr<TradingCalendar> calendar)
{
    // get the first two chars of the asset id
    std::string first_two = this->asset_id.substr(0, 2);
    std::expected<long long, AgisException> res;
	switch (this->_parent_contract) {
	case FutureParentContract::ES:
		res = calendar->es_future_contract_to_expiry(this->asset_id);
	case FutureParentContract::CL:
		res = calendar->cl_future_contract_to_expiry(this->asset_id);
	case FutureParentContract::ZF:
		res = calendar->zf_futures_contract_to_first_intention(this->asset_id);
	default:
		return std::unexpected<AgisException>(AGIS_EXCEP("Invalid future code"));
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
