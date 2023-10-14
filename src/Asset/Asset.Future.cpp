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
	static const std::unordered_map<char, FutureMonthCode> month_map = {
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

	auto it = month_map.find(month_code);
	if (it != month_map.end()) {
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
	static std::map<std::string, FutureParentContract> contract_map = {
		{"ES", FutureParentContract::ES},
		{"CL", FutureParentContract::CL},
		{"ZF", FutureParentContract::ZF}
	};

	auto it = contract_map.find(this->asset_id.substr(0, 2));
	if (it != contract_map.end()) {
		this->_parent_contract = it->second;
		return true;
	}
	else {
		return std::unexpected<AgisException>("Invalid future code");
	}
}

//============================================================================
[[nodiscard]] std::expected<bool, AgisException> Future::__build(Exchange const* exchange) noexcept
{
	if (this->asset_id.length() != 7)
	{
		return std::unexpected<AgisException>("invalid futures contract: " + this->asset_id);
	}
	return this->set_future_code()
		.and_then([&](bool const& b) {return this->set_future_parent_contract(); })
		.and_then([&](bool const& b) {return this->set_last_trade_date(exchange->get_trading_calendar()); }
	);
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
		break;
	case FutureParentContract::CL:
		res = calendar->cl_future_contract_to_expiry(this->asset_id);
		break;
	case FutureParentContract::ZF:
		res = calendar->zf_futures_contract_to_first_intention(this->asset_id);
		break;
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
    std::string contract_id) : AssetTable(exchange), _contract_id(contract_id)
{
    this->p = new FuturePrivate(exchange);
}

} // namespace Agis
