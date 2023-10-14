#include <string>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <expected>
#include <stdexcept>
#include <chrono>

#include <boost/date_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "AgisException.h"

#include "Time/TradingCalendar.h"

using namespace std::chrono;
using namespace boost::gregorian;
using namespace boost::posix_time;


namespace Agis
{

//============================================================================
std::expected<bool, AgisException>
TradingCalendar::load_holiday_file(std::string const& file_path)
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

				date d(year, month, day );
				this->_holidays.push_back(std::move(d));
			}
		}
		file.close();

		// sort the holidays earliest to latest
		std::sort(this->_holidays.begin(), this->_holidays.end());
		return true;
	}
	catch (const std::exception& e) {
		return std::unexpected<AgisException>("Invalid holiday file: " + file_path + ": " + e.what());
	}
}

//============================================================================
bool
TradingCalendar::is_holiday(date const& date_obj) const noexcept
{
	return std::binary_search(this->_holidays.begin(), this->_holidays.end(), date_obj);
}


//============================================================================
bool
TradingCalendar::is_business_day(date const& date_obj) const noexcept
{
	return !this->is_holiday(date_obj) &&
		greg_weekday(date_obj.day_of_week()).as_enum() != boost::gregorian::greg_weekday::weekday_enum::Saturday &&
		greg_weekday(date_obj.day_of_week()).as_enum() != boost::gregorian::greg_weekday::weekday_enum::Sunday;
}


//============================================================================
std::expected<bool, AgisException>
TradingCalendar::is_valid_date(int year, int month, int day) const noexcept
{
	if (year < 1900 || year > 2100) {
		return std::unexpected<AgisException>("Invalid year: " + std::to_string(year));
	}
	if (month < 1 || month > 12) {
		return std::unexpected<AgisException>("Invalid month: " + std::to_string(month));
	}
	if (day < 1 || day > 31) {
		return std::unexpected<AgisException>("Invalid day: " + std::to_string(day));
	}
	return true;
}



//============================================================================
date
TradingCalendar::get_previous_business_day(date const& d)
{
	return this->business_days_subtract(d, 1);
}


//============================================================================
date
TradingCalendar::business_days_subtract(date const& d, uint16_t n)
{
	date d1 = d;
	int counter = 0;
	while (true) {
		d1 -= boost::gregorian::days(1);
		if (greg_weekday(d1.day_of_week()).as_enum() != boost::gregorian::greg_weekday::weekday_enum::Saturday &&
			greg_weekday(d1.day_of_week()).as_enum() != boost::gregorian::greg_weekday::weekday_enum::Sunday &&
			!this->is_holiday(d1)){
			counter++;
		}
		if (counter == n) {
			break;
		}
	}
	return d1;
}

//============================================================================
std::expected<long long, AgisException>
TradingCalendar::cl_future_contract_to_expiry(std::string contract_id)
{
	// expect contract_id to be in the following formats:
	// 1. CLZ2020 (Dec 2020)
	// check length
	if (contract_id.length() != 7) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	// verify first two characters are ES
	if (contract_id.substr(0, 2) != "CL") {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	// get the month code
	char month_code = contract_id[2];
	// get the month number
	uint16_t month_int = future_month_code_to_int(month_code);
	if (month_int == 0) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	uint16_t year_int;
	try {
		year_int = std::stoi(contract_id.substr(3, 4));
	}
	catch (std::invalid_argument const& e) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id + ": " + e.what());
	}
	//For crude oil, each contract expires on the third business day prior to 
	// the 25th calendar day of the month preceding the delivery month.
	// If the 25th calendar day of the month is a non-business day, trading ceases on the
	// third business day prior to the business day preceding the 25th calendar day.
	if (month_int == 1) {
		month_int = 12;
		year_int -= 1;
	}
	else {
		month_int -= 1;
	}
	auto res = this->is_valid_date(year_int, month_int, 25);
	if(!res.has_value()){
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}

	date d1(year_int, month_int, 25);
	// find the first business day before the 25th. Return 6 PM EST
	if (!this->is_business_day(d1)) {
		d1 = this->get_previous_business_day(d1);
	}
	d1 = this->business_days_subtract(d1, 3);
	tm t = to_tm(d1);
	t.tm_hour = 18;
	t.tm_min = 00;
	t.tm_sec = 0;
	// convert to nanosecond epoch time
	auto tp2 = system_clock::from_time_t(std::mktime(&t));
	auto ns = duration_cast<nanoseconds>(tp2.time_since_epoch());
	// return as long long
	return ns.count();
}


//============================================================================
std::expected<long long, AgisException>
TradingCalendar::zf_futures_contract_to_first_intention(std::string contract_id)
{
	auto expiration_opt = zf_future_contract_to_date(contract_id);
	if (!expiration_opt.has_value()) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	date expiration = expiration_opt.value();
	// First Intention Day, also known as First Position Day, is the second business day before
	// the first business day of the expiring contract’s delivery month. Return 6 PM EST
	// find the first day of the expiration month
	auto res = this->is_valid_date(expiration.year(), expiration.month(), 1);
	if (!res.has_value()) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	date d1(expiration.year(), expiration.month(), 1);
	int counter = 0;
	while (true) {
		d1 -= boost::gregorian::days(1);
		if (greg_weekday(d1.day_of_week()).as_enum() == boost::gregorian::greg_weekday::weekday_enum::Saturday ||
			greg_weekday(d1.day_of_week()).as_enum() == boost::gregorian::greg_weekday::weekday_enum::Sunday ||
			this->is_holiday(d1))
		{
			continue;
		}
		counter++;
		if (counter == 2) {
			break;
		}
	}
	tm t = to_tm(d1);
	t.tm_hour = 18;
	t.tm_min = 00;
	t.tm_sec = 0;
	// convert to nanosecond epoch time
	auto tp2 = system_clock::from_time_t(std::mktime(&t));
	auto ns = duration_cast<nanoseconds>(tp2.time_since_epoch());
	// return as long long
	return ns.count();
}


//============================================================================
std::expected<long long, AgisException>
TradingCalendar::zf_future_contract_to_expiry(std::string contract_id)
{
	auto date_opt = zf_future_contract_to_date(contract_id);
	if (!date_opt.has_value()) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	auto d1 = date_opt.value();
	tm t = to_tm(d1);
	t.tm_hour = 12;
	t.tm_min = 01;
	t.tm_sec = 0;
	// convert to nanosecond epoch time
	auto tp2 = system_clock::from_time_t(std::mktime(&t));
	auto ns = duration_cast<nanoseconds>(tp2.time_since_epoch());
	// return as long long
	return ns.count();
	
}


//============================================================================
std::expected<date, AgisException>
TradingCalendar::zf_future_contract_to_date(std::string contract_id)
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
	auto res = this->is_valid_date(year_int, month_int, 1);
	if (!res.has_value()) {
		return std::unexpected<AgisException>("Invalid contract id: " + contract_id);
	}
	date d0(year_int, month_int, 1 );
	date d1 = d0.end_of_month();
	while (
		greg_weekday(d1.day_of_week()).as_enum() == boost::gregorian::greg_weekday::weekday_enum::Saturday ||
		greg_weekday(d1.day_of_week()).as_enum() == boost::gregorian::greg_weekday::weekday_enum::Sunday ||
		this->is_holiday(d1)
		) {
		d1 -= boost::gregorian::days(1);
	}
	return d1;
}


//============================================================================
std::expected<long long, AgisException>
TradingCalendar::es_future_contract_to_expiry(std::string contract_id)
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
TradingCalendar::future_month_code_to_int(char month_code)
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