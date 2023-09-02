#include "pch.h"

#include <limits>
#include <cmath>

#include "AgisFunctional.h"
#include "AgisStrategy.h"


//============================================================================
const std::function<double(double, double)> agis_init = [](double a, double b) { return b; };
const std::function<double(double, double)> agis_identity = [](double a, double b) { return a; };
const std::function<double(double, double)> agis_add = [](double a, double b) { return a + b; };
const std::function<double(double, double)> agis_subtract = [](double a, double b) { return a - b; };
const std::function<double(double, double)> agis_multiply = [](double a, double b) { return a * b; };
const std::function<double(double, double)> agis_divide = [](double a, double b) { return a / b; };


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
	for (const auto& operation : operations) {

		// apply the lambda function to the asset and extract the value
		if (!operation.is_filter())
		{
			auto& asset_lambda = operation.get_asset_operation();
			asset_lambda_res = asset_lambda(asset);
			if (asset_lambda_res.is_exception()) return asset_lambda_res;
		}
		else {
			auto& asset_filter = operation.get_asset_filter();
			auto res = asset_filter(result);
			if (!res) return AgisResult<double>(AGIS_NAN);
		}
		// if operation is filter then also check for Nan results meaning to exclude the asset
		auto x = asset_lambda_res.unwrap();
		if(std::isnan(x)) return AgisResult<double>(AGIS_NAN);
		result = operation.l.first(result, asset_lambda_res.unwrap());

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
		AgisResult<double> val = std::get<AssetOpperation>(assetFeatureLambda)(asset);
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
