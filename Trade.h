#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "pch.h"

class Order;
typedef std::unique_ptr<Order> OrderPtr;

class AgisStrategy;
typedef std::unique_ptr<AgisStrategy> AgisStrategyPtr;
typedef std::reference_wrapper<AgisStrategyPtr> AgisStrategyRef;


struct Trade;
class TradeExit;

AGIS_API typedef std::unique_ptr<Trade> TradePtr;
AGIS_API typedef std::unique_ptr<TradeExit> TradeExitPtr;
AGIS_API typedef std::reference_wrapper<const TradePtr> TradeRef;


struct AGIS_API Trade {
    double units;
    double average_price;
    double close_price;
    double last_price;
    double nlv;

    double unrealized_pl;
    double realized_pl;

    long long trade_open_time;
    long long trade_close_time;
    size_t bars_held;

    size_t trade_id;
    size_t asset_id;
    size_t strategy_id;
    size_t portfolio_id;
    AgisStrategyRef strategy;

    std::optional<TradeExitPtr> exit = std::nullopt;

    Trade(AgisStrategyRef strategy, OrderPtr const& order);

    void close(OrderPtr const& filled_order);
    void increase(OrderPtr const& filled_order);
    void reduce(OrderPtr const& filled_order);
    void adjust(OrderPtr const& filled_order);
    void evaluate(double market_price, bool on_close);

    OrderPtr generate_trade_inverse();

private:
    static std::atomic<size_t> trade_counter;
};


class TradeExit {
public:
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

    bool AGIS_API exit() override {
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