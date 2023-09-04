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
class Asset;
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


struct AGIS_API Trade {
    AssetPtr __asset;           ///< pointer to the underlying asset of the trade
    AgisStrategy* strategy;     ///< raw pointer to the strategy that generated the trade

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

    /// <summary>
    /// Boolean flag to test if the strategy has allocated touch the trade. Used 
    /// for clearing trades that were not in the new allocation of a strategy.
    /// </summary>
    bool strategy_alloc_touch = false;

    std::optional<TradeExitPtr> exit = std::nullopt;

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
    AgisResult<json> serialize(json& _json, HydraPtr hydra) const;

    [[nodiscard]] size_t get_asset_index() const { return this->asset_index; }
    [[nodiscard]] size_t get_strategy_index() const { return this->strategy_index; }
    [[nodiscard]] size_t get_portfolio_index() const { return this->portfolio_index; }

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

    virtual void build(Trade const* trade_) { this->trade = trade_; }

    AGIS_API virtual bool exit() = 0;

    virtual TradeExit* clone() const { 
        // not good idea, but have to get the py trampoline class to work
        return nullptr; 
    };

    Order* take_child_order() { return std::move(this->child_order.value()); }
    bool has_child_order() const { return this->child_order.has_value(); }
    void insert_child_order(Order* child_order_) { this->child_order = std::move(child_order_); }

protected:
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
        auto res = (this->trade->bars_held >= this->bars);
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
        if (this->stop_loss_pct.has_value()) {
            this->stop_loss_pct = (1 + this->stop_loss_pct.value()) * trade->last_price;
        }
        if (this->take_profit_pct.has_value()) {
            this->take_profit_pct = (1 + this->take_profit_pct.value()) * trade->last_price;
        }
        TradeExit::build(trade_);
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