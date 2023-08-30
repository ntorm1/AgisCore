#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <memory>
#include <variant>
#include "AgisErrors.h"

class Portfolio;
class AgisStrategy;

struct Drawdown {
    double maxDrawdown;
    long long longestDrawdownStart;
    long long longestDrawdownEnd;
};

 class PortfolioStats
{
    friend class AgisStrategy;
    friend class Portfolio;
public:
    AGIS_API PortfolioStats(Portfolio* portolio, double cash, double risk_free = 0);
    AGIS_API PortfolioStats(AgisStrategy* strategy, double cash, double risk_free = 0);

    double risk_free = 0;
    std::span<long long> dt_index;
    std::variant<Portfolio*, AgisStrategy*> entity;

    std::vector<double> beta_history;
    std::vector<double> nlv_history;
    std::vector<double> cash_history;

    double net_beta = 0;
    double nlv = 0;
    double cash = 0;
    double starting_cash = 0;

    bool is_beta_tracing = false;

    void __reserve(size_t n);
    void __evaluate();
    void __reset();


    AGIS_API double get_stats_total_pl() const;
    AGIS_API double get_stats_pct_returns() const;
    AGIS_API double get_stats_annualized_pct_returns() const;
    AGIS_API double get_stats_annualized_volatility() const;
    AGIS_API double get_stats_sharpe_ratio() const;
    AGIS_API Drawdown get_stats_drawdown() const;
    AGIS_API std::vector<double> get_stats_rolling_drawdown() const;
    AGIS_API std::vector<double> get_stats_underwater_plot() const;
    AGIS_API std::vector<double> get_rolling_sharpe(size_t window_size = 252) const;

};