#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "pch.h"
#include "AgisErrors.h"

class Hydra;
class Order;

namespace Agis {
    class Asset;
    class Broker;
}

using namespace Agis;

typedef std::shared_ptr<Asset> AssetPtr;
typedef std::unique_ptr<Order> OrderPtr;
typedef const Hydra* HydraPtr;


class AgisStrategy;
typedef std::unique_ptr<AgisStrategy> AgisStrategyPtr;
typedef std::reference_wrapper<AgisStrategyPtr> MAgisStrategyRef;


struct Trade;
class TradeExit;

AGIS_API typedef std::shared_ptr<Trade> SharedTradePtr;
AGIS_API typedef std::shared_ptr<TradeExit> TradeExitPtr;
AGIS_API typedef std::reference_wrapper<const SharedTradePtr> TradeRef;


/**
 * @brief A trade partition is a chunk of a trade that belongs to a trade in a different asset. I.e. every trade
 * might have a beta hedge associated with it, and those trade are responsible some portion of the beta hedge trade.
*/
struct TradePartition {
    Trade* parent_trade         = nullptr;
    Trade* child_trade          = nullptr;
    double child_trade_units    = 0.0f;
};


struct AGIS_API Trade {
    AssetPtr __asset;         ///< pointer to the underlying asset of the trade
    AgisStrategy* strategy;   ///< raw pointer to the strategy that generated the trade
    Broker* broker;           ///< raw pointer to the broker that the trade was placed to

    double units;
    double average_price;
    double open_price;
    double close_price;
    double last_price;
    double nlv;

    double unrealized_pl;
    double realized_pl;

    long long trade_open_time;
    long long trade_close_time;
    size_t bars_held;

    size_t trade_id;
    size_t asset_index;
    size_t strategy_index;
    size_t portfolio_index;

    /**
     * @brief Boolean flag to test if the strategy has allocated touch the trade. Used 
     * for clearing trades that were not in the new allocation of a strategy.
    */
    bool strategy_alloc_touch = false;

    /**
     * @brief an optional trade exit object that will be used to close the trade if a condition is met
    */
    std::optional<TradeExitPtr> exit = std::nullopt;

    /**
     * @brief a vector of child trade partitions that contain chunks of a trade in a different asset that belong to this trade.
    */
    std::vector<std::shared_ptr<TradePartition>> child_partitions;

    Trade(AgisStrategy* strategy, OrderPtr const& order);

    void close(OrderPtr const& filled_order);
    void increase(OrderPtr const& filled_order);
    void reduce(OrderPtr const& filled_order);
    void adjust(OrderPtr const& filled_order);
    void evaluate(
        double market_price,
        bool on_close,
        bool is_reprice = false
    );
    OrderPtr generate_trade_inverse();
    std::expected<rapidjson::Document, AgisException> serialize(HydraPtr hydra) const;

    void take_partition(std::shared_ptr<TradePartition> partition) { this->child_partitions.push_back(std::move(partition)); };
    [[nodiscard]] std::shared_ptr<TradePartition> get_child_partition(size_t asset_index);
    [[nodiscard]] bool partition_exists(size_t asset_index);

    [[nodiscard]] size_t get_asset_index() const noexcept { return this->asset_index; }
    [[nodiscard]] size_t get_strategy_index() const noexcept { return this->strategy_index; }
    [[nodiscard]] size_t get_portfolio_index() const noexcept { return this->portfolio_index; }
    inline static void __reset_counter() { trade_counter.store(0); }
private:
    static std::atomic<size_t> trade_counter;
};


enum class AGIS_API TradeExitType
{
   BARS,
   THRESHOLD,
};


class TradeExit {
public:
    virtual ~TradeExit() = default;
    TradeExit() : trade(nullptr) {}

    /**
     * @brief function to build the trade exit using the parent trade pointer. Function is 
     * called on initialization of the trade. 
     * @param trade_ 
    */
    virtual void build(Trade const* trade_) { this->trade = trade_; }

    AGIS_API virtual bool exit() = 0;

    virtual TradeExit* clone() const { 
        // not good idea, but have to get the py trampoline class to work
        return nullptr; 
    };

    /**
     * @brief take the child order from the trade exit and assune ownership of the order.
     * The order is the placed as needed.
     * @return 
    */
    Order* take_child_order() { return std::move(this->child_order.value()); }

    /**
     * @brief does the trade exit have a child order
     * @return true if the trade exit has a child order, false otherwise
    */
    bool has_child_order() const { return this->child_order.has_value(); }
    
    /**
     * @brief insert a new child order into the trade exit. The child order is placed on the
     * trade exit being true.
     * @param child_order_ 
    */
    void insert_child_order(Order* child_order_) {
        this->child_order = std::move(child_order_); 
    }

protected:
    /**
     * @brief a point to a child exit representing the next exit in the chain. exits 
     * evaluated from the top level down and result and a exit being true if either one, some, or
     * all trade exits in a chain are true depnding on and or flag.
    */
    std::optional<TradeExitPtr> child_exit = std::nullopt;

    /**
	 * @brief boolean flag to determine if the child exit is an and or or. 
    */
    bool child_exit_is_and = true;

    /**
     * @brief parent trade of the trade exit
    */
    Trade const* trade;

    /**
     * @brief an optional child order to be placed on trade exit
    */
    std::optional<Order*> child_order = std::nullopt;
};


/**
 * @brief Exit a trade when the number of bars held is equal to the number of bar defined
*/
class ExitBars : public TradeExit {
public:
    ExitBars(size_t bars_) : TradeExit() {
        this->bars = bars_;
    }

    AGIS_API inline bool exit() override {
        auto res = (this->trade->bars_held == this->bars);
        return res;
    }

    // Implement the clone function to create a deep copy
    ExitBars* clone() const override {
        return new ExitBars(*this);
    }

private:
    size_t bars;

};


/**
 * @brief Exit a trade when the % change in price is greater than the specified value
*/
class ExitThreshold : public TradeExit {
public:
    /**
     * @brief Exit threshould constructor
     * @param stop_loss_pct_ % decline in the price that will trigger a trade close (-.05) = 5% decline
     * @param take_profit_pct_ % increase in the price that will trigger a trade close (.05) = 5% increase
    */
    ExitThreshold(std::optional<double> stop_loss_pct_, std::optional<double> take_profit_pct_) : TradeExit() {
        this->stop_loss_pct = stop_loss_pct_;
        this->take_profit_pct = take_profit_pct_;
    }

    AGIS_API void build(Trade const* trade_) override {
        TradeExit::build(trade_);
        if (this->stop_loss_pct.has_value()) {
            this->stop_loss_pct = (1 + this->stop_loss_pct.value()) * this->trade->last_price;
        }
        if (this->take_profit_pct.has_value()) {
            this->take_profit_pct = (1 + this->take_profit_pct.value()) * this->trade->last_price;
        }
    }


    AGIS_API inline bool exit() override {
        if (this->trade->last_price <= this->stop_loss_pct) { return true; }
        if (this->trade->last_price >= this->take_profit_pct) { return true; }
        return false;
    }

    // Implement the clone function to create a deep copy
    ExitThreshold* clone() const override {
        return new ExitThreshold(*this);
    }


private:
    /**
     * @brief Stop loss pct is initial defined as a the % decline from the open price 
     * that will trigger a trade close. After it is built it is reassigned to the actual threshould 
    */
    std::optional<double> stop_loss_pct;

    /**
     * @brief Take profit pct is initial defined as a the % increase from the open price
     * that will trigger a trade close. After it is built it is reassigned to the actual threshould
    */
    std::optional<double> take_profit_pct;
};

/**
 * @brief Exit a trade if the last price falls outside of the specified bounds
*/
class ExitBand : public TradeExit {
public:
    ExitBand(double ub_, double lb_) : TradeExit() {
        this->ub = ub_;
        this->lb = lb_;
    }

    bool exit() override {
        if (this->trade->last_price <= lb) { return true; }
        if (this->trade->last_price >= ub) { return true; }
        return false;
    }

    // Implement the clone function to create a deep copy
    ExitBand* clone() const override {
        return new ExitBand(*this);
    }

private:
    double lb;
    double ub;

};