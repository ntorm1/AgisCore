#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

/// <summary>
/// An enumeration representing different order types
/// </summary>
enum class AGIS_API OrderType
{
    MARKET_ORDER,                  /**< market order*/
    LIMIT_ORDER,                   /**< limit order */
    STOP_LOSS_ORDER,               /**< stop loss order */
    TAKE_PROFIT_ORDER              /**< take profit order */
};

/// <summary>
/// brief An enumeration representing the current start of an order
/// </summary>
enum class AGIS_API OrderState
{
    PENDING,  /// order has been created but yet to be sent
    OPEN,     /// order is open on the exchange
    FILLED,   /// order has been filled by the exchange
    CANCELED, /// order has been canceled by a strategy
    REJECTED, /// order was rejected by the checker 
    CHEAT,    /// allows orders we know will fill to be filled and processed in single router call
};


/// <summary>
/// brief An enumeration representning the execution type of the order. An order can either
/// be sent by the broker as soon as it recieves it, or it can wait till the end of the open / close period.
/// </summary>
enum class AGIS_API OrderExecutionType
{
    EAGER, /// order will be placed as soon as the broker gets it
    LAZY   /// order will be placed in broker send orders sweep
};


/// <summary>
/// An enumeration representing the type of order target used for portfolio target functions.
/// Allows portfolio to easily place orders expressed in various units, not just number of shares or amount of the underlying.
/// </summary>
enum class AGIS_API OrderTargetType
{
    UNITS,              /// order target size is in units, i.e. 100 shares
    DOLLARS,            /// order target size is in dollars, i.e. $1000 at $100 a share => 10 shares
    PCT,                /// order target size is in pct of total nlv of the source portfolio
    BETA_DOLLARS,       /// order target size is in beta dollars 
    PCT_BETA_DOLLARS    /// order target size is in pct of total nlv of the source potfolio normalized by beta
};


/// <summary>
/// An enumeration representing the type of parent used for order's that require an order parent.
/// Orders like take - profit or stop - loss require a parent, either an open order or an open trade.
/// </summary>
enum class AGIS_API OrderParentType
{
    TRADE, /// parent of the order is a smart pointer to a trade
    ORDER  /// parent of the order is a smart pointer to another open order
};


/// <summary>
/// Enum for the data frequency of an asset. 
/// </summary>
enum class AGIS_API Frequency {
    Tick,    // Tick data
    Min1,    // 1 minute data
    Min5,    // 5 minute data
    Min15,   // 15 minute data
    Min30,   // 30 minute data
    Hour1,   // 1 hour data
    Hour4,   // 4 hour data
    Day1,    // 1 day data
};


enum class AGIS_API AgisStrategyType {
    CPP,			// a c++ based strategy directly inheriting from AgisStrategy
    FLOW,			// a flow strategy used by the Nexus Node Editor to create Abstract strategies
    PY,				// a python strategy tramploine off PyAgisStrategy from AgisCorePy
    BENCHMARK,		// a benchmark strategy that does not interfer with portfolio values
    LUAJIT, 		// a lua jit strategy
};


enum class AGIS_API AssetType
{
    US_EQUITY,
    US_FUTURE,
};