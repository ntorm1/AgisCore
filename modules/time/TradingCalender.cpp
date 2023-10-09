module;
#include <string>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <expected>
#include <stdexcept>
#include <chrono>

#include <boost/date_time.hpp>

#include "AgisException.h"


module TradingCalender;

using namespace std::chrono;
using namespace boost::gregorian;


namespace Agis
{

//============================================================================
std::expected<bool, AgisException>
TradingCalender::load_holiday_file(std::string const& file_path)
{
	this->_holidays.clear();

	// verify file exists
	std::ifstream file(file_path);
	if (!file.good()) {
		return std::unexpected<AgisException>("File does not exist: " + file_path);
	}
	try {
		// Skip the first line (header)
		std::string header;
		std::getline(file, header);

		std::string line;
		while (std::getline(file, line)) {
			std::stringstream ss(line);
			std::string name, dateStr;

			while (std::getline(ss, name, ',')) {
				std::getline(ss, dateStr, ',');

				// hack to deal with comma in holiday name
				if (dateStr == " ") {
					std::getline(ss, dateStr, ',');
				}
				if (dateStr.find_first_not_of("0123456789") != std::string::npos) {
					std::getline(ss, dateStr, ',');
				}

				// Extract day, month, and year
				int day, month, year;
				std::istringstream dateStream(dateStr);
				dateStream >> day;
				dateStream.ignore(); // Ignore the delimiter (usually a comma)
				dateStream >> month;
				dateStream.ignore(); // Ignore the delimiter
				dateStream >> year;

				if (this->_holidays.size() >= 55) {
					auto y = 2;
				}

				date d(year, month, day );
				this->_holidays.push_back(std::move(d));
			}
		}
		file.close();
		return true;
	}
	catch (const std::exception& e) {
		return std::unexpected<AgisException>("Invalid holiday file: " + file_path + ": " + e.what());
	}
}


//============================================================================
std::expected<long long, AgisException>
TradingCalender::zf_future_contract_to_expiry(std::string contract_id)
{
	// expect contract_id to be in the following formats:
	// 1. ZFZ2020 (Dec 2020)
	// check length
	if (contract_id.length() != 7) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	// verify first two characters are ES
	if (contract_id.substr(0, 2) != "ZF") {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	// get the month code
	char month_code = contract_id[2];
	// verify it is in [H,M,U,Z]
	if (month_code != 'H' && month_code != 'M' && month_code != 'U' && month_code != 'Z') {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	// get the month and year number
	uint16_t month_int = future_month_code_to_int(month_code);
	uint16_t year_int;
	try {
		year_int = std::stoi(contract_id.substr(3, 4));
	}
	catch (std::invalid_argument const& e) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id + ": " + e.what());
	}
	// ZF futures expire at 12:01 PM CT on the last business day of the contract month
	date d0{ year_int, year_int, 0 };
	date d1 = d0.end_of_month();
	while (!greg_weekday(d1.day_of_week())) {
		d1 -= boost::gregorian::days(2);
	}
}


//============================================================================
std::expected<long long, AgisException>
TradingCalender::es_future_contract_to_expiry(std::string contract_id)
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
	year_month_day date{ year(year_int) / month(month_int) / std::chrono::Friday[3] };
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
TradingCalender::future_month_code_to_int(char month_code)
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