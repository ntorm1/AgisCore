#pragma once


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
		return std::unexpected<AgisException>(AGIS_EXCEP("Invalid month code: " + month_code));
	}
	return true;
}


//============================================================================
std::expected<bool, AgisException>
Future::set_future_parent_contract()
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
std::expected<double, AgisErrorCode> Future::get_volatility() const
{
	auto current_time = this->__get_asset_time(true);
	auto& continous_dt = this->_table->get_continous_dt_vec();
	auto it = std::find(continous_dt.begin(), continous_dt.end(), current_time);
	if (it == continous_dt.end()) {
		return std::unexpected<AgisErrorCode>(AgisErrorCode::OUT_OF_RANGE);
	}
	auto idx = it - continous_dt.begin();
	return this->_table->get_continous_vol_vec()[idx];
}


//============================================================================
std::expected<bool, AgisException>
Future::__set_volatility(size_t lookback)
{
	this->__set_warmup(lookback);
	if (!this->_table) return true; // skip assets outside of table
	if (!this->_table->get_continous_vol_vec().size()) {
		this->_table->__set_volatility(lookback);
	}
	return true;
}


//============================================================================
std::expected<bool, AgisException>
Future::__build(Exchange const* exchange) noexcept
{
	if (_last_trade_date.has_value()) return true;
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
bool Future::__is_last_view(long long t) const noexcept
{
	// if on last row force last view return true
	if (Asset::__is_last_view(t)) return true;
	// if this is the last trade date for the future return true
	if (this->_last_trade_date.has_value()) {
		// less then or equals prevents the case when the underlying data of an asset
		// exceedes the last trade date of the future. I.e. a future contract with a Dec 2018
		// expiry somehow has a datapoint in 2019. With strict equality this will never hit.
		return this->_last_trade_date <= t;
	}
	// ele allow last view to be true
	return false;
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
std::expected<FuturePtr, AgisErrorCode> FutureTable::front_month()
{
	if (this->_tradeable.size() == 0) {
		return std::unexpected<AgisErrorCode>(AgisErrorCode::OUT_OF_RANGE);
	}
	// return fron of table case as Future 
	return std::dynamic_pointer_cast<Future>(this->_tradeable.front());
}


//============================================================================
FutureTable::FutureTable(
    Exchange* exchange,
    std::string contract_id) : AssetTable(exchange)
{
	this->_contract_id = contract_id;
    this->p = new FuturePrivate(exchange);
}


//============================================================================
std::expected<bool, AgisException> FutureTable::__set_volatility(size_t t) noexcept
{
	if (t >= this->_continous_close_vec.size()) {
		return std::unexpected<AgisException>(AGIS_EXCEP("Invalid lookback"));
	}
	auto cont_close_span = std::span<const double>(this->_continous_close_vec);
	this->_continout_vol_vec = rolling_volatility(cont_close_span, t);
	return true;
}


//============================================================================
void FutureTable::__set_child_ptrs() noexcept
{
	// set table pointer to child assets
	for (auto const& asset : this->_tradeable) {
		auto future = std::dynamic_pointer_cast<Future>(asset);
		future->_table = this;
	}
	for (auto const& asset : this->_out_of_bounds) {
		auto future = std::dynamic_pointer_cast<Future>(asset);
		future->_table = this;
	}
}


//============================================================================
std::expected<bool, AgisException> FutureTable::__build()
{
	// build the deque tables 
	auto res = AssetTable::__build();
	if (!res.has_value()) {
		return std::unexpected<AgisException>(res.error());
	}
	// set the asset table pointers for each child asset
	this->__set_child_ptrs();

	// TODO: assumes build before warmup
	if (_continous_close_vec.size()) {
		return true;
	}

	auto exchange_lock = this->_exchange->__write_lock();
	auto exchange_dt_index = this->_exchange->__get_dt_index();
	ThreadSafeVector<size_t> expired_assets;
	FuturePtr current_asset = nullptr;
	
	_continous_close_vec.clear();
	_continous_dt_vec.clear();
	// TODO: make all futures table update at once instead of one at a time
	for (size_t i = 0; i < exchange_dt_index.size(); ++i) {
		this->_exchange->step(expired_assets);
		auto current_time = this->_exchange->__get_market_time();
		if (this->_tradeable.size()) {
			FuturePtr front = this->front_month().value();
			if (!current_asset) {
				current_asset = front;
			}
			// front month rolled to new contract
			else if (front != current_asset) {
				auto idx = current_asset->__get_close_index();
				double prev_close = current_asset->__get_column(idx).back();
				double current_close = front->__get_market_price(true);
				double adjustemnt = current_close - prev_close;
				// subtract all values in close_vec by adjustment
				for (auto& close : _continous_close_vec) {
					close -= adjustemnt;
				}
				current_asset = front;
			}
			_continous_close_vec.push_back(front->__get_market_price(true));
			_continous_dt_vec.push_back(current_time);
		}
		else {
			_continous_close_vec.push_back(0.0);
			_continous_dt_vec.push_back(current_time);
		}
	}
	this->_exchange->reset();
	return true;
}

} // namespace Agis
