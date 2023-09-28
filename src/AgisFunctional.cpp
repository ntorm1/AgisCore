#include "pch.h"

#include <limits>
#include <cmath>

#include "AgisFunctional.h"
#include "AgisStrategy.h"


//============================================================================
const AgisOperation agis_init = [](double a, double b) { return b; };
const AgisOperation agis_identity = [](double a, double b) { return a; };
const AgisOperation agis_add = [](double a, double b) { return a + b; };
const AgisOperation agis_subtract = [](double a, double b) { return a - b; };
const AgisOperation agis_multiply = [](double a, double b) { return a * b; };
const AgisOperation agis_divide = [](double a, double b) { return a / b; };

//============================================================================
const AgisLogicalOperation agis_greater_than = [](double a, double b) { return a > b; };
const AgisLogicalOperation agis_less_than = [](double a, double b) { return a < b; };
const AgisLogicalOperation agis_greater_than_or_equal = [](double a, double b) { return a >= b; };
const AgisLogicalOperation agis_less_than_or_equal = [](double a, double b) { return a <= b; };
const AgisLogicalOperation agis_equal = [](double a, double b) { return a == b; };
const AgisLogicalOperation agis_not_equal = [](double a, double b) { return a != b; };



//============================================================================
std::unordered_map<std::string, AgisOperation> agis_function_map = {
   {"INIT", agis_init},
   {"IDENTITY", agis_identity},
   {"ADD", agis_add},
   {"SUBTRACT", agis_subtract},
   {"MULTIPLY", agis_multiply},
   {"DIVIDE", agis_divide}
};


//============================================================================
std::unordered_map<std::string, ExchangeQueryType> agis_query_map = {
   {"Default", ExchangeQueryType::Default},
   {"NLargest", ExchangeQueryType::NLargest},
   {"NSmallest", ExchangeQueryType::NSmallest },
   {"NExtreme", ExchangeQueryType::NExtreme},
};


//============================================================================
std::vector<std::string>  agis_query_strings =
{
	"Default",	/// return all assets in view
	"NLargest",	/// return the N largest
	"NSmallest",/// return the N smallest
	"NExtreme"	/// return the N/2 smallest and largest
};


//============================================================================
std::vector<std::string> agis_function_strings = {
	"INIT",
	"IDENTITY",
	"ADD",
	"SUBTRACT",
	"MULTIPLY",
	"DIVIDE"
};


//============================================================================
std::vector<std::string> agis_strat_alloc_strings = {
	//"UNITS",
	//"DOLLARS",
	"PCT"
};


//============================================================================
std::vector<std::string> agis_trade_exit_strings = {
	"BARS",
	"THRESHOLD"
};


//============================================================================
std::vector<std::string> agis_trading_windows = {
	"",
	"US_EQUITY_REG_HRS"
};


//============================================================================
std::unordered_map<std::string, TradingWindow> agis_trading_window_map = {
	{"US_EQUITY_REG_HRS", us_equity_reg_hrs}
};



//============================================================================
const std::function<AgisResult<double>(
	const std::shared_ptr<Asset>&,
	const std::vector<AssetLambdaScruct>& operations)> asset_feature_lambda_chain = [](
		const std::shared_ptr<Asset>& asset,
		const std::vector<AssetLambdaScruct>& operations
		)
{
	// loop through the asset lambda structs and apply the operations
	double result = 0;
	AgisResult<double> asset_lambda_res;
	for (const auto& asset_lambda : operations) {

		// apply the lambda function to the asset and extract the value
		if (!asset_lambda.is_filter())
		{
			auto& asset_opperation = asset_lambda.get_asset_operation();
			asset_lambda_res = asset_opperation(asset);
			if (asset_lambda_res.is_exception()) return asset_lambda_res;
		}
		else {
			auto& asset_filter = asset_lambda.get_asset_filter();
			auto res = asset_filter(result);
			if (!res) return AgisResult<double>(AGIS_NAN);
			continue;
		}
		// if operation is filter then also check for Nan results meaning to exclude the asset
		auto x = asset_lambda_res.unwrap();
		if(std::isnan(x)) return AgisResult<double>(AGIS_NAN);
		result = asset_lambda.get_agis_operation()(result, asset_lambda_res.unwrap());

	}
	asset_lambda_res.set_value(result);
	return asset_lambda_res;
};


//============================================================================
const std::function<AgisResult<double>(
	const std::shared_ptr<Asset>&,
	const std::vector<AssetLambda>& operations)> concrete_lambda_chain = [](
		const std::shared_ptr<Asset>& asset,
		const std::vector<AssetLambda>& operations
		)
{
	double result = 0;
	for (const auto& lambda : operations) {
		const auto& op = lambda.first;
		const auto& assetFeatureLambda = lambda.second;
		AgisResult<double> val = assetFeatureLambda(asset);
		// test to see if the lambda returned an exception
		if (val.is_exception()) return val;
		// test to see if the lambda returned NaN
		auto x = val.unwrap();
		if (std::isnan(x)) return val;
		result = op(result, x);
	}
	return AgisResult<double>(result);
};


//============================================================================
std::string trading_window_to_key_str(std::optional<TradingWindow> input_window_opt) {
	if (!input_window_opt.has_value()) { return ""; }
	auto input_window = input_window_opt.value();
	for (const auto& entry : agis_trading_window_map) {
		const TradingWindow& window = entry.second;
		if (window.second == input_window.second)
		{
			return entry.first;
		}
	}
	return ""; // Return an empty string if no match is found
}


TradingWindow us_equity_reg_hrs = {
	{9,30},
	{16,0}
};


//============================================================================
TradingWindow all_hrs = {
	{0,0},
	{23,59}
};


//============================================================================
std::unordered_map<std::string, AllocType> agis_strat_alloc_map = {
   {"UNITS", AllocType::UNITS},
   {"DOLLARS", AllocType::DOLLARS},
   {"PCT", AllocType::PCT},
};


//============================================================================
std::unordered_map<std::string, TradeExitType> trade_exit_type_map = {
	{ "BARS", TradeExitType::BARS},
	{ "THRESHOLD", TradeExitType::THRESHOLD}
};


//============================================================================
std::string alloc_to_str(AllocType alloc_type)
{
	static const std::map<AllocType, std::string> typeStrings = {
		{AllocType::UNITS, "UNITS"},
		{AllocType::DOLLARS, "DOLLARS"},
		{AllocType::PCT, "PCT"}
	};

	auto it = typeStrings.find(alloc_type);
	if (it != typeStrings.end()) return it->second;
	return "UNKNOWN";
}


//============================================================================
AgisResult<ExchangeViewOpp> str_to_ev_opp(const std::string& ev_opp_type) {
	static const std::map<std::string, ExchangeViewOpp> ev_opp_type_map = {
		{"UNIFORM", ExchangeViewOpp::UNIFORM},
		{"LINEAR_DECREASE", ExchangeViewOpp::LINEAR_DECREASE},
		{"LINEAR_INCREASE", ExchangeViewOpp::LINEAR_INCREASE},
		{"CONDITIONAL_SPLIT", ExchangeViewOpp::CONDITIONAL_SPLIT},
		{"UNIFORM_SPLIT", ExchangeViewOpp::UNIFORM_SPLIT},
		{"CONSTANT", ExchangeViewOpp::CONSTANT}
	};

	auto it = ev_opp_type_map.find(ev_opp_type);
	if (it != ev_opp_type_map.end()) {
		return AgisResult<ExchangeViewOpp>(it->second);
	}

	// Handle the case when the input string is not found in the map
	return AgisResult<ExchangeViewOpp>(AGIS_EXCEP("Invalid ExchangeViewOpp value: " + ev_opp_type));
}
