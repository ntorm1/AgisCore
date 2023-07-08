#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "pch.h"

#include "Order.h"

struct Trade;
class TradeExit;

AGIS_API typedef std::unique_ptr<Trade> TradePtr;
AGIS_API typedef std::unique_ptr<TradeExit> TradeExitPtr;



struct AGIS_API Trade {
    double units;
    double average_price;
    double close_price;
    double last_price;

    double unrealized_pl;
    double realized_pl;

    long long trade_open_time;
    long long trade_close_time;
    size_t bars_held;

    size_t trade_id;
    size_t asset_id;
    size_t strategy_id;
    size_t portfolio_id;

    std::optional<TradeExitPtr> exit = std::nullopt;

    Trade(OrderPtr const& order);

    void close(OrderPtr const& filled_order);
    void increase(OrderPtr const& filled_order);
    void reduce(OrderPtr const& filled_order);
    void adjust(OrderPtr const& filled_order);

    OrderPtr generate_trade_inverse();

private:
    static std::atomic<size_t> trade_counter;
};


class TradeExit {
public:
    TradeExit(TradePtr const& trade_) : trade(trade_) {}

    AGIS_API virtual bool exit() = 0;

protected:
    TradePtr const& trade;
};

class ExitBars : public TradeExit {
public:
    ExitBars(TradePtr const& trade_, size_t bars_) : TradeExit(trade_) {
        this->bars = bars_;
    }

    bool AGIS_API exit() override { return this->bars == this->trade->bars_held; }

private:
    size_t bars;

};
