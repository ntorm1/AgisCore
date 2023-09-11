#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "AgisErrors.h"
#include <bitset>
#include <cstdint>
#include <iostream>
#include <Eigen/Dense>

using namespace Eigen;

class AgisStrategy;

enum Tracer : uint16_t {
    NLV = 0,
    CASH = 1,
    LEVERAGE = 2,
    BETA = 3,
    VOLATILITY = 4,
    MAX = 5
};

class AgisStrategyTracers {
    friend class AgisStrategy;
public:


    AGIS_API AgisStrategyTracers(AgisStrategy* strategy_) {
        this->strategy = strategy_;
    };
    AGIS_API AgisStrategyTracers(AgisStrategy* strategy_, std::initializer_list<Tracer> opts) {
        this->strategy = strategy_;
        for (const Tracer& opt : opts)
            value_.set(opt);
    }
    bool has(Tracer o) const { return value_[o]; }
    std::optional<double> get(Tracer o) const;
    void set(Tracer o) { value_.set(o); }
    void reset(Tracer o) { value_.reset(o); }

    AgisResult<bool> evaluate();
    void set_portfolio_weight(size_t index, double v);
    void zero_out_tracers();
    void build(AgisStrategy* strategy, size_t n);
    void reset_history();

    void cash_add_assign(double v) { this->cash += v; }
    void nlv_add_assign(double v) { this->nlv += v; }
    void net_beta_add_assign(double v) { this->net_beta += v; }
    void net_leverage_ratio_add_assign(double v) { this->net_leverage_ratio += v; }

protected:
    std::vector<double> nlv_history;
    std::vector<double> cash_history;
    std::vector<double> net_leverage_ratio_history;
    std::vector<double> portfolio_volatility_history;

    double nlv = 0;
    double cash = 0;
    double starting_cash = 0;

    VectorXd portfolio_weights;

private:
    double net_beta = 0;
    double net_leverage_ratio = 0;
    double portfolio_volatility = 0;

    AgisStrategy* strategy;
    std::bitset<MAX> value_{ 0 };

    std::vector<double> beta_history;

};