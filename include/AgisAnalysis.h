#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <memory>
#include <deque>
#include <span>
#include <vector>
#include <math.h>
#include "AgisErrors.h"


 struct Drawdown {
    double maxDrawdown;
    long long longestDrawdownStart;
    long long longestDrawdownEnd;
};


//============================================================================
AGIS_API double get_stats_total_pl(std::vector<double> const& nlv_history);

//============================================================================
AGIS_API double get_stats_pct_returns(std::vector<double> const& nlv_history);

//============================================================================
AGIS_API double get_stats_annualized_pct_returns(std::vector<double> const& nlv_history);

//============================================================================
AGIS_API double get_stats_annualized_volatility(std::vector<double> const& nlv_history);

//============================================================================
AGIS_API double get_stats_sharpe_ratio(std::vector<double> const& nlv_history, double risk_free = 0.0f);

//============================================================================
AGIS_API AgisResult<double> get_stats_beta(
    std::vector<double> const& nlv_history,
    std::vector<double> const& benchmark_nlv_history    
);

//============================================================================
AGIS_API Drawdown get_stats_drawdown(
    std::vector<double> const& nlv_history,
    std::span<long long> dt_index
);

//============================================================================
AGIS_API std::vector<double> get_rolling_sharpe(
    std::vector<double> const& nlv_history,
    size_t window_size,
    double risk_free = 0
);

//============================================================================
AGIS_API std::vector<double> get_stats_underwater_plot(std::span<double const> const& nlv_history);

//============================================================================
AGIS_API std::vector<double> get_stats_rolling_drawdown(std::vector<double> const& nlv_history);