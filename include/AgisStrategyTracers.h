#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "AgisErrors.h"
#include <bitset>
#include <cstdint>
#include <atomic>
#include <iostream>
#include <Eigen/Dense>

using namespace Eigen;

class AgisStrategy;
class Portfolio;

enum Tracer : size_t {
    NLV = 0,
    CASH = 1,
    LEVERAGE = 2,
    BETA = 3,
    VOLATILITY = 4,
    MAX = 5
};

class AgisStrategyTracers {
    friend class AgisStrategy;
    friend class Portfolio;
public:


    AGIS_API AgisStrategyTracers(AgisStrategy* strategy_);

    AGIS_API AgisStrategyTracers(Portfolio* portfolio, double cash);

    AGIS_API AgisStrategyTracers(AgisStrategy* strategy_, std::initializer_list<Tracer> opts) {
        this->strategy = strategy_;
        for (const Tracer& opt : opts)
            value_.set(opt);
    }

    inline bool has(Tracer o) const noexcept { return value_[o]; }
    std::optional<double> get(Tracer o) const;
    void set(Tracer o) { value_.set(o); }
    void reset(Tracer o) { value_.reset(o); }

    AgisResult<bool> evaluate();
    void set_portfolio_weight(size_t index, double v);
    void zero_out_tracers();


    //============================================================================
    template <typename T>
    void build(T* owner, size_t n)
    {
        if (this->has(Tracer::BETA)) this->beta_history.reserve(n);

        if (this->has(Tracer::LEVERAGE)) this->net_leverage_ratio_history.reserve(n);

        if (this->has(Tracer::VOLATILITY)) {
            // init eigen vector of portfolio weights
            this->portfolio_volatility_history.reserve(n);
            auto asset_count = owner->exchange_map->get_asset_count();
            this->portfolio_weights.resize(asset_count);
            this->portfolio_weights.setZero();
        }

        this->cash.store(this->starting_cash.load());
        this->nlv.store(this->cash.load());

        if (this->has(Tracer::NLV)) this->nlv_history.reserve(n);
        if (this->has(Tracer::CASH)) this->cash_history.reserve(n);
    }
    
    void reset_history();

    inline void cash_add_assign(double v) noexcept { this->cash.fetch_add(v); }
    inline void nlv_add_assign(double v) noexcept { this->nlv.fetch_add(v); }
    inline void net_beta_add_assign(double v) noexcept { this->net_beta.fetch_add(v); }
    inline void net_leverage_ratio_add_assign(double v) noexcept { this->net_leverage_ratio.fetch_add(v); }

protected:
    std::vector<double> nlv_history;
    std::vector<double> cash_history;
    std::vector<double> net_leverage_ratio_history;
    std::vector<double> portfolio_volatility_history;

    std::atomic<double> nlv = 0;
    std::atomic<double> cash = 0;
    std::atomic<double> starting_cash = 0;

    VectorXd portfolio_weights;

private:
    AgisResult<double> get_portfolio_volatility();
    AgisResult<double> get_benchmark_volatility();

    std::atomic<double> net_beta = 0;
    std::atomic<double> net_leverage_ratio = 0;
    std::atomic<double> portfolio_volatility = 0;

    AgisStrategy* strategy;
    std::bitset<MAX> value_{ 0 };

    std::vector<double> beta_history;

};