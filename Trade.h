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


class TradeExit {
public:
    virtual ~TradeExit() = default;
    TradeExit() : trade(nullptr) {}

    void build(Trade const* trade_) { this->trade = trade_; }

    AGIS_API virtual bool exit() = 0;

protected:
    Trade const* trade;
};


class ExitBars : public TradeExit {
public:
    ExitBars(size_t bars_) : TradeExit() {
        this->bars = bars_;
    }

    AGIS_API inline bool  exit() override {
        auto res = this->bars == this->trade->bars_held; 
        return res;
    }

private:
    size_t bars;

};


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

private:
    double lb;
    double ub;

};