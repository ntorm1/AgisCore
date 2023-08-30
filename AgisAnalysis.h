#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <memory>

#include "AgisErrors.h"

class Portfolio;

struct Drawdown {
    double maxDrawdown;
    long long longestDrawdownStart;
    long long longestDrawdownEnd;
};

 class PortfolioStats
{
    friend class Portfolio;
public:
    AGIS_API PortfolioStats(Portfolio* portolio, double risk_free = 0);

    double risk_free = 0;
    std::span<long long> dt_index;
    Portfolio* portfolio;

    std::vector<double> const& nlv_history;
    std::vector<double> const& cash_history;

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