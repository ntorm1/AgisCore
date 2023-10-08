module;
#include <string>
#include <cstdint>
#include <expected>
#include <stdexcept>
#include <chrono>

#include "AgisException.h"

module Asset:Future;

import :Core;

using namespace std::chrono;

namespace Agis
{

//============================================================================
std::expected<long long, AgisException>
cl_future_contract_to_expiry(std::string contract_id)
{
	return 0;
}


//============================================================================
std::expected<long long, AgisException>
es_future_contract_to_expiry(std::string contract_id)
{
	// expect contract_id to be in the following formats:
	// 1. ESZ2020 (Dec 2020)
	// check length
	if (contract_id.length() != 7) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	// verify first two characters are ES
	if (contract_id.substr(0, 2) != "ES") {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	// get the month code
	char month_code = contract_id[2];
	// verify it is in [H,M,U,Z]
	if (month_code != 'H' && month_code != 'M' && month_code != 'U' && month_code != 'Z') {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	// get the month number
	uint16_t month_int = future_month_code_to_int(month_code);
	// get the year
	uint16_t year_int;
	try {
		year_int = std::stoi(contract_id.substr(3, 4));
	}
	catch (std::invalid_argument const& e) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id + ": " + e.what());
	}
	// ES futures expiry at 9:30 AM EST on the third Friday of the contract month
	// get the third Friday of the month
	year_month_day date{ year(year_int) / month(month_int) / Friday[3] };
	// convert to system_clock::time_point
	auto tp = sys_days(date);
	// convert to time_t
	auto tt = system_clock::to_time_t(tp);
	// convert to tm
	auto tm = gmtime(&tt);
	// Determine the time zone offset. Hackey but it works as ES expires quarterly
	int dst_offest = (month_int >= 3 && month_int <= 11) ? -1 : 0;
	tm->tm_hour = 9 + dst_offest;
	tm->tm_min = 30;
	tm->tm_sec = 0;
	// convert to nanosecond epoch time
	auto tp2 = system_clock::from_time_t(std::mktime(tm));
	auto ns = duration_cast<nanoseconds>(tp2.time_since_epoch());
	// return as long long
	return ns.count();
}


//============================================================================
uint16_t
future_month_code_to_int(char month_code)
{
	switch (month_code) {
	case 'F':
		return 1;
	case 'G':
		return 2;
	case 'H':
		return 3;
	case 'J':
		return 4;
	case 'K':
		return 5;
	case 'M':
		return 6;
	case 'N':
		return 7;
	case 'Q':
		return 8;
	case 'U':
		return 9;
	case 'V':
		return 10;
	case 'X':
		return 11;
	case 'Z':
		return 12;
	default:
		return 0;
	}
}

}