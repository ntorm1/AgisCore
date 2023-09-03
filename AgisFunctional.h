#pragma once
#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "AgisErrors.h"
#include "Exchange.h"


constexpr double AGIS_NAN = std::numeric_limits<double>::quiet_NaN();

class TradeExit;

/**
 * @brief An Agis operation operates on two doubles and returns a double. Used for opperating over elements
 * in an exchange to extract a single value from an asset given a series of operations.
*/
AGIS_API typedef std::function<double(double, double)> AgisOperation;

/**
 * @brief return back the second element passed
*/
AGIS_API extern const AgisOperation agis_init;

/**
 * @brief returns the first element passed
*/
AGIS_API extern const AgisOperation agis_identity;

/**
 * @brief adds the two elements passed
*/
AGIS_API extern const AgisOperation agis_add;

/**
 * @brief subtracts the second element passed from the first
*/
AGIS_API extern const AgisOperation agis_subtract;

/**
 * @brief multiplies the two elements passed
*/
AGIS_API extern const AgisOperation agis_multiply;

/**
 * @brief divides the first element passed by the second
*/
AGIS_API extern const AgisOperation agis_divide;

/**
 * @brief Takes an agis operation and returns a string representation of it
 * @param func ref to an agis operation
 * @return string representation of the agis operation
*/
AGIS_API std::string opp_to_str(const AgisOperation& func);

/**
 * @brief Function that takes in str information about a trade exit and returns a TradeExitPtr
 * @param trade_exit_type type of trade exit "BARS", "THRESHOLD"
 * @param trade_exit_params a string of params used to build the exit
 * @return unique pointer to the new trade exit
*/
AGIS_API AgisResult<TradeExitPtr> parse_trade_exit(
    TradeExitType trade_exit_type,
    const std::string& trade_exit_params
);

/**
 * @brief A an agis range is parses a string representation of range like [0.2,.3) and
 * defines a bool function that returns wether or not a passed number is within the range
*/
struct AssetFilterRange {
public:
    AGIS_API AssetFilterRange() = default;
    AGIS_API AssetFilterRange(const std::string& range_str_) {
        // ugly try catch but it works, prevents invalid range strings from being use
        try {
            parse_range(range_str_);
            this->range_str = range_str_;
        }
        catch (std::exception& e) {
            throw e;
        }
    }

    AGIS_API std::string code_gen() const {
        std::string filter_str = R"(AssetLambdaScruct(AssetFilterRange({FILTER_STR})))";
        auto pos = filter_str.find("{FILTER_STR}");
        filter_str.replace(pos, 12, this->range_str);
        return filter_str;
    }
    
    // function thar returns a lambda function that captures the within_range function by value
    AGIS_API std::function<bool(double)> get_filter() const {
        double lowerBound = lowerBound_;
        double upperBound = upperBound_;
        bool lowerInclusive = lowerInclusive_;
        bool upperInclusive = upperInclusive_;

        return [=](double value) {
            if (lowerInclusive && upperInclusive) {
                return value >= lowerBound && value <= upperBound;
            }
            else if (lowerInclusive && !upperInclusive) {
                return value >= lowerBound && value < upperBound;
            }
            else if (!lowerInclusive && upperInclusive) {
                return value > lowerBound && value <= upperBound;
            }
            else { // Both bounds are exclusive
                return value > lowerBound && value < upperBound;
            }
        };
    }


private:
    double lowerBound_;
    double upperBound_;
    bool lowerInclusive_;
    bool upperInclusive_;
    std::string range_str;
    void parse_range(const std::string& rangeStr) {
        try {
            // Check if the first character is '[' (inclusive) or '(' (exclusive)
            lowerInclusive_ = (rangeStr[0] == '[');
            upperInclusive_ = (rangeStr[rangeStr.size() - 1] == ']');

            // Remove leading '[' or '(' and trailing ']' or ')'
            std::string trimmedStr = rangeStr.substr(1, rangeStr.size() - 2);

            // Split the string by ','
            std::istringstream ss(trimmedStr);
            std::string lowerStr, upperStr;
            std::getline(ss, lowerStr, ',');
            std::getline(ss, upperStr, ',');

            // Convert lower and upper bounds to double
            lowerBound_ = std::stod(lowerStr);
            upperBound_ = std::stod(upperStr);
            if (ss.fail()) {
                throw std::runtime_error("Failed to parse range string.");
            }
        }
        catch (std::exception& e) {
            throw e;
        }
    }
};


AGIS_API typedef std::function<AgisResult<double>(const std::shared_ptr<Asset>&)> AssetOpperation;
AGIS_API typedef std::function<bool(double)> AssetFilter;


/**
 * @brief An AssetLambda is a pair of an AgisOperation and a lambda function that takes a shared_ptr to an Asset.
 * It first applies the function to the asset and the applies the opperations to the output of the function. They
 * can be strung together in a sequence to extract a single value from an asset (i.e. the 1d % return)
 * 
 * Optionally if can be a filter operation that takes a double that is the result of the previous operation
 * and returns a bool indicating wether or not the asset should be included in the result set.
*/
AGIS_API typedef std::pair<AgisOperation, AssetOpperation> AssetLambda;


struct AssetOpperationStruct {
    AssetLambda asset_lambda;
    std::string column;
    int row;
};


struct AssetFilterStruct {
    std::pair<AgisOperation, AssetFilter> asset_lambda;
    AssetFilter filter;
    AssetFilterRange asset_filter_range;
};


/**
 * @brief A struct containing information needed to execute a single opperation on an asset
*/
struct AGIS_API AssetLambdaScruct {
    AssetLambdaScruct(AssetFilterRange asset_filter_range) {
        auto asset_lambda = std::make_pair(agis_identity, asset_filter_range.get_filter());
        AssetFilterStruct filter_struct{ asset_lambda, asset_filter_range.get_filter(), asset_filter_range};
        this->l = filter_struct;
    }

    AssetLambdaScruct(AssetLambda asset_lambda, AgisOperation opp, std::string column, int row) {
        auto x = AssetOpperationStruct{ asset_lambda, column, row };
        this->l = std::move(x);
	}


    AssetOpperation const& get_asset_operation() const{
        return std::get<AssetOpperationStruct>(l).asset_lambda.second;
    }

    auto const& get_asset_operation_struct() const {
        return std::get<AssetOpperationStruct>(l);
    }

    auto const& get_asset_filter() const {
        return std::get<AssetFilterStruct>(l).asset_lambda.second;
    }

    auto const& get_asset_filter_struct() const {
        return std::get<AssetFilterStruct>(l);
    }

    auto const& get_agis_operation() const {
        return std::get<AssetOpperationStruct>(l).asset_lambda.first;
    }

    bool is_filter() const {
		return std::holds_alternative<AssetFilterStruct>(l);
	}

	bool is_operation() const {
		return std::holds_alternative<AssetOpperationStruct>(l);
	}

    std::variant<AssetOpperationStruct, AssetFilterStruct> l;
};


AGIS_API typedef std::vector<AssetLambdaScruct> AgisAssetLambdaChain;
AGIS_API typedef std::function<double(AssetPtr const&)> ExchangeViewOperation;
AGIS_API typedef std::function<ExchangeView(
	AgisAssetLambdaChain const&,
	ExchangePtr const,
	ExchangeQueryType,
	int)
> ExchangeViewLambda;


enum class AGIS_Function {
	INIT,		// returns the element in the second position
	IDENTITY,	// returns the element in the first position
	ADD,		// addition
	SUBTRACT,	// subtraction
	MULTIPLY,	// multiply
	DIVIDE		// divide
};



AGIS_API typedef const std::pair<TimePoint, TimePoint> TradingWindow;

extern AGIS_API TradingWindow us_equity_reg_hrs;
extern AGIS_API TradingWindow all_hrs;

/**
 * @brief Mapping between a string and an AgisOperation i.e. "ADD" -> agis_add
*/
extern AGIS_API std::unordered_map<std::string, AgisOperation> agis_function_map;

/**
 * @brief A vector if strings that contains all the valid AgisOperations
*/
extern AGIS_API std::vector<std::string> agis_function_strings;

/**
 * @brief A mapping between a string and an ExchangeQueryType i.e. "NLargest" -> ExchangeQueryType::NLargest
*/
extern AGIS_API std::unordered_map<std::string, ExchangeQueryType> agis_query_map;
extern AGIS_API std::vector<std::string> agis_query_strings;
extern AGIS_API std::vector<std::string> agis_strat_alloc_strings;
extern AGIS_API std::vector<std::string> agis_trade_exit_strings;
extern AGIS_API std::vector<std::string> agis_trading_windows;
extern AGIS_API std::unordered_map<std::string, TradingWindow> agis_trading_window_map;
