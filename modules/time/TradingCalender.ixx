module;

#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <string>
#include <memory>
#include <expected>
#include "AgisException.h"

#include <boost/date_time.hpp>

export module TradingCalender;

using namespace boost::gregorian;

export namespace Agis
{

class TradingCalender {
public:
	TradingCalender() = default;

	date get_previous_business_day(date const& d);
	date business_days_subtract(date const& d, uint16_t n);
	AGIS_API bool is_holiday(date const& date_obj) const noexcept;
	AGIS_API bool is_business_day(date const& date_obj) const noexcept;

	AGIS_API std::vector<date> const& holidays() const noexcept {return this->_holidays;}
	AGIS_API std::expected<bool, AgisException> load_holiday_file(std::string const& file_path);
	AGIS_API std::expected<long long, AgisException> es_future_contract_to_expiry(std::string contract_id);
	AGIS_API std::expected<long long, AgisException> zf_future_contract_to_expiry(std::string contract_id);
	AGIS_API std::expected<long long, AgisException> cl_future_contract_to_expiry(std::string contract_id);
	AGIS_API std::expected<date, AgisException> zf_future_contract_to_date(std::string contract_id);
	AGIS_API std::expected<long long, AgisException> zf_futures_contract_to_first_intention(std::string contract_id);
	uint16_t future_month_code_to_int(char month_code);
	
private:
	std::string _calender_file_path;
	std::vector<date> _holidays;
};
}