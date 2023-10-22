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
 * @brief enum class representing the type allocation for an exchange view
 */
enum class AGIS_API AllocType
{
    UNITS,		// set strategy portfolio to have N units
    DOLLARS,	// set strategy portfolio to have $N worth of units
    PCT			// set strategy portfolio to have %N worth of units (% of nlv)
};


/**
 * @brief enum class representing the type of target to apply when transforming an exchange view
*/
enum class AGIS_API AllocTypeTarget
{
    LEVERAGE,   // exchange view target to taget a net leverage for the strategy
    VOL         // exchange view target to target a volatility for the strategy
};


/**
 * @brief An Agis operation operates on two doubles and returns a double. Used for opperating over elements
 * in an exchange to extract a single value from an asset given a series of operations.
*/
AGIS_API typedef std::function<double(double, double)> AgisOperation;

/**
 * @brief An Agis logical operation operates on two doubles and returns a bool. Used for opperating over elements
 * and comparing them to a constant value
*/
AGIS_API typedef std::function<bool(double, double)> AgisLogicalOperation;

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

AGIS_API std::string OppToString(const AgisOperation& func);
AGIS_API std::string AllocToString(AllocType alloc_type);
AGIS_API const char* OrderStateToString(OrderState value);
AGIS_API const char* OrderTypeToString(OrderType value);
AGIS_API const char* AgisStrategyTypeToString(AgisStrategyType value);
AGIS_API AgisStrategyType StringToAgisStrategyType(const std::string& typeStr);

/**
 * @brief takes a string like "UNIFORM" and returns the enum in a result
*/
AgisResult<ExchangeViewOpp> str_to_ev_opp(const std::string& ev_opp_type);

/**
 * @brief mapping between string and a trade exit type
*/
extern AGIS_API std::unordered_map<std::string, TradeExitType> trade_exit_type_map;

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
        std::string filter_str = R"(AssetLambdaScruct(AssetFilterRange("{FILTER_STR}")))";
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


AGIS_API typedef std::function<std::expected<double,AgisStatusCode>(const std::shared_ptr<Asset>&)> AssetOpperation;
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


//============================================================================
enum class AgisOpperationType {
	INIT,		// returns the element in the second position
	IDENTITY,	// returns the element in the first position
	ADD,		// addition
	SUBTRACT,	// subtraction
	MULTIPLY,	// multiply
	DIVIDE		// divide
};


//============================================================================
enum class AgisLogicalType {
    GREATER_THAN,           // returns true if the element is greater C  
    LESS_THAN,              // returns true if the element is less than C
    GREATER_THAN_EQUAL,     // returns true if the element is greater than or equal to C
    LESS_THAN_EQUAL,        // returns true if the element is less than or equal to C
    EQUAL,                  // returns true if the element is equal to C
    NOT_EQUAL               // returns true if the element is not equal to C    
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


/**
 * @brief hold the information regarding how an abstract strategy allocates it's portfolio
*/
struct AGIS_API StrategyAllocLambdaStruct {
    double epsilon; /// he minimum % difference between target position size and current position size needed to trigger a new order
    double target;	/// The targert portfolio leverage used to apply weights to the allocations

    std::optional<double> ev_extra_opp = std::nullopt;		/// Optional extra param to pass to ev weight application function
    std::optional<TradeExitPtr> trade_exit = std::nullopt;	/// Optional trade exit point to apply to new trades

    bool clear_missing;					///Clear any positions that are currently open but not in the allocation
    std::string ev_opp_type;			/// Type of weights to apply to the exchange view
    AllocType alloc_type;				/// type of allocation to use	
    AllocTypeTarget alloc_type_target;	/// type of target allocation to use
};



/**
 * @brief Hold the information neded for an abstract strategy to call the get_exchange_view function
 */
struct AGIS_API ExchangeViewLambdaStruct {
    /**
     * @brief The number of assets to query for
    */
    int N;

    /**
     * @brief The warmup period needed for the lambda chain to be valid
    */
    size_t warmup;

    /**
     * @brief the lambda chain representing the operation to apply to each asset
    */
    AgisAssetLambdaChain asset_lambda;

    /**
     * @brief the lambda function that executes the get_exchange_view function
    */
    ExchangeViewLambda exchange_view_labmda;

    /**
     * @brief shred pointer to the underlying exchange
    */
    ExchangePtr exchange;

    /**
     * @brief type of query to do, used for sorting asset values
    */
    ExchangeQueryType query_type;

    /**
    * @brief optional strategy alloc struct containing info about how to process the
    * exchange view into portfolio weights
    */
    std::optional<StrategyAllocLambdaStruct> strat_alloc_struct = std::nullopt;
};

