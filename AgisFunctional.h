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
 * @brief An AssetLambda is a pair of an AgisOperation and a lambda function that takes a shared_ptr to an Asset.
 * It first applies the function to the asset and the applies the opperations to the output of the function. They
 * can be strung together in a sequence to extract a single value from an asset (i.e. the 1d % return)
*/
AGIS_API typedef std::pair<AgisOperation, std::function<AgisResult<double>(const std::shared_ptr<Asset>&)>> AssetLambda;


/**
 * @brief A an agis range is parses a string representation of range like [0.2,.3) and
 * defines a bool function that returns wether or not a passed number is within the range
*/
struct AssetFilter {
public:
    AGIS_API AssetFilter() = default;
    AGIS_API AssetFilter(const std::string& rangeStr) {
        try {
            parse_range(rangeStr);
        }
        catch (std::exception& e) {
            throw e;
        }
    }

    AGIS_API bool within_range(double value) const {
        if (lowerInclusive_ && upperInclusive_) {
            return value >= lowerBound_ && value <= upperBound_;
        }
        else if (lowerInclusive_ && !upperInclusive_) {
            return value >= lowerBound_ && value < upperBound_;
        }
        else if (!lowerInclusive_ && upperInclusive_) {
            return value > lowerBound_ && value <= upperBound_;
        }
        else { // Both bounds are exclusive
            return value > lowerBound_ && value < upperBound_;
        }
    }

private:
    double lowerBound_;
    double upperBound_;
    bool lowerInclusive_;
    bool upperInclusive_;

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

/**
 * @brief A struct containing information needed to execute a single opperation on an asset
*/
struct AGIS_API AssetOpperation {
    AssetOpperation() = default;
    AssetOpperation(AssetLambda l, AgisOperation opp, std::string column, int row) {
		this->l = l;
		this->opp = opp;
		this->column = column;
		this->row = row;
	}
    AssetLambda l;
    AgisOperation opp;
    std::string column;
    int row;
};

/**
 * @brief A struct containing information needed to execute either a Asset Lambda operation or an
 * asset filter operation
*/
struct AGIS_API AssetLambdaScruct {
    ~AssetLambdaScruct() {}
    AssetLambdaScruct() = default;
    AssetLambdaScruct(AssetFilter asset_filter_) {
        this->asset_lambda = asset_filter_;
    }
    AssetLambdaScruct(AssetOpperation asset_operation_) {
        this->asset_lambda = asset_operation_;
    }

    bool is_filter() const {
		return std::holds_alternative<AssetFilter>(this->asset_lambda);
	}

    bool valid_asset(double x) const {
        assert(this->is_filter());
        return std::get<AssetFilter>(this->asset_lambda).within_range(x);
    }

    int row() const {
        assert(!this->is_filter());
        return std::get<AssetOpperation>(this->asset_lambda).row;
    }

    std::string const& column() const {
        assert(!this->is_filter());
        return std::get<AssetOpperation>(this->asset_lambda).column;
    }

    AssetLambda const& l() const {
		assert(!this->is_filter());
		return std::get<AssetOpperation>(this->asset_lambda).l;
	}

    AgisOperation opp() const {
        assert(!this->is_filter());
        return std::get<AssetOpperation>(this->asset_lambda).opp;
    }

    std::variant<AssetFilter , AssetOpperation> asset_lambda;
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

extern AGIS_API std::unordered_map<std::string, AgisOperation> agis_function_map;
extern AGIS_API std::vector<std::string> agis_function_strings;
extern AGIS_API std::unordered_map<std::string, ExchangeQueryType> agis_query_map;
extern AGIS_API std::vector<std::string> agis_query_strings;
extern AGIS_API std::vector<std::string> agis_strat_alloc_strings;
extern AGIS_API std::vector<std::string> agis_trading_windows;
extern AGIS_API std::unordered_map<std::string, TradingWindow> agis_trading_window_map;
