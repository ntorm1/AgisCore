#include "pch.h"
#include <numeric>
#include "AgisAnalysis.h"
#include "Portfolio.h"
#include "AgisStrategy.h"


//============================================================================
double get_stats_total_pl(std::vector<double> const& nlv_history)
{
    return nlv_history.back() - nlv_history.front();
}


//============================================================================
double get_stats_pct_returns(std::vector<double> const& nlv_history)
{
    return 100 * get_stats_total_pl(nlv_history) / nlv_history.front();
}


//============================================================================
double get_stats_annualized_pct_returns(std::vector<double> const& nlv_history)
{
    // calculate annualized returns
    auto days = nlv_history.size();
    auto years = days / 252.0;
    return 100 * (pow(nlv_history.back() / nlv_history.front(), 1 / years) - 1);
}


//============================================================================
double get_stats_annualized_volatility(std::vector<double> const& nlv_history)
{
    // calculate annualized volatility
    auto days = nlv_history.size();
    auto years = days / 252.0;
    auto daily_returns = std::vector<double>(days - 1);
    for (size_t i = 0; i < days - 1; i++)
    {
        daily_returns[i] = (nlv_history[i + 1] - nlv_history[i]) / nlv_history[i];
    }
    auto mean = std::accumulate(daily_returns.begin(), daily_returns.end(), 0.0) / daily_returns.size();
    auto sq_sum = std::inner_product(daily_returns.begin(), daily_returns.end(), daily_returns.begin(), 0.0);
    auto stdev = std::sqrt(sq_sum / daily_returns.size() - mean * mean);
    return 100 * stdev * std::sqrt(252);
}


//============================================================================
double get_stats_sharpe_ratio(std::vector<double> const& nlv_history, double risk_free)
{
    // Calculate the Sharpe Ratio
    return (get_stats_annualized_pct_returns(nlv_history) - risk_free)
        / get_stats_annualized_volatility(nlv_history);
}


//============================================================================F
Drawdown get_stats_drawdown(
    std::vector<double> const& nlv_history,
    std::span<long long> dt_index
)
{
    Drawdown result = { 0.0, 0, 0 };
    size_t n = nlv_history.size();

    double peak = nlv_history[0];
    double trough = nlv_history[0];
    long long peakTime = dt_index[0];
    long long troughTime = dt_index[0];
    long long longestDrawdownStart = dt_index[0];
    long long longestDrawdownEnd = dt_index[0];

    for (int i = 1; i < n; ++i) {
        if (nlv_history[i] > peak) {
            peak = nlv_history[i];
            peakTime = dt_index[i];
            trough = nlv_history[i];
            troughTime = dt_index[i];
        }
        else if (nlv_history[i] < trough) {
            trough = nlv_history[i];
            troughTime = dt_index[i];
            if (peak - trough > result.maxDrawdown) {
                result.maxDrawdown = peak - trough;
                result.longestDrawdownStart = longestDrawdownStart;
                result.longestDrawdownEnd = longestDrawdownEnd;
            }
        }
        else if (nlv_history[i] >= trough && nlv_history[i] <= peak) {
            if (peak - trough > result.maxDrawdown) {
                result.maxDrawdown = peak - trough;
                result.longestDrawdownStart = longestDrawdownStart;
                result.longestDrawdownEnd = longestDrawdownEnd;
            }
        }

        if (dt_index[i] - troughTime > longestDrawdownEnd - longestDrawdownStart) {
            longestDrawdownStart = troughTime;
            longestDrawdownEnd = dt_index[i];
        }
    }
    return result;
}


//============================================================================
AgisResult<double> get_stats_beta(std::vector<double> const& nlv_history, std::vector<double> const& benchmark_nlv_history)
{
    if(nlv_history.size() != benchmark_nlv_history.size())
	{
		return AgisResult<double>(AGIS_EXCEP("nlv_history and benchmark_nlv_history must have the same size"));
	}

    double covariance = 0.0;
    double benchmark_variance = 0.0;

    double nlv_mean = 0.0;
    double benchmark_mean = 0.0;

    for (size_t i = 1; i < nlv_history.size(); ++i) {
        double nlv_return = (i > 0) ? (nlv_history[i] - nlv_history[i - 1]) / nlv_history[i - 1] : 0.0;
        double benchmark_return = (i > 0) ? (benchmark_nlv_history[i] - benchmark_nlv_history[i - 1]) / benchmark_nlv_history[i - 1] : 0.0;

        nlv_mean += nlv_return;
        benchmark_mean += benchmark_return;

        covariance += nlv_return * benchmark_return;
        benchmark_variance += benchmark_return * benchmark_return;
    }

    nlv_mean /= nlv_history.size();
    benchmark_mean /= benchmark_nlv_history.size();

    double beta = (covariance - nlv_mean * benchmark_mean * nlv_history.size()) /
        (benchmark_variance - benchmark_mean * benchmark_mean * nlv_history.size());

    return AgisResult<double>(beta);
}


//============================================================================
std::vector<double> get_rolling_sharpe(
    std::vector<double> const& nlv_history,
    size_t window_size,
    double risk_free)
{
    std::vector<double> returns;
    std::vector<double> rolling_sharpe_ratios;

    double rolling_avg_return = 0.0;
    double rolling_std_dev_sum = 0.0;

    for (size_t i = 1; i <= nlv_history.size(); ++i) {
        double current_return = (nlv_history[i] - nlv_history[i - 1]) / nlv_history[i - 1];
        returns.push_back(current_return);

        rolling_avg_return += current_return;
        rolling_std_dev_sum += current_return * current_return;

        if (i >= window_size) {
            if (i > window_size) {
                double removed_return = (nlv_history[i - window_size] - nlv_history[i - window_size - 1])
                    / nlv_history[i - window_size - 1];
                rolling_avg_return -= removed_return;
                rolling_std_dev_sum -= removed_return * removed_return;
            }

            double avg_return = rolling_avg_return / window_size;
            double std_dev = std::sqrt((rolling_std_dev_sum - window_size * avg_return * avg_return) / (window_size - 1));

            double annualized_avg_return = avg_return * 252; // Assuming 252 trading days in a year
            double annualized_std_dev = std_dev * std::sqrt(252); // Annualize standard deviation

            double sharpe_ratio = (annualized_avg_return - risk_free) / annualized_std_dev;
            rolling_sharpe_ratios.push_back(sharpe_ratio);
        }
    }
    return rolling_sharpe_ratios;
}


//============================================================================

std::vector<double> get_stats_underwater_plot(std::span<double const> const& nlv_history) {
    auto n = nlv_history.size();
    std::vector<double> underwater_plot(n, 0.0);

    double peak = nlv_history[0];
    for (size_t i = 0; i < n; ++i) {
        if (nlv_history[i] > peak) {
            peak = nlv_history[i];
        }
        underwater_plot[i] = (nlv_history[i] - peak) / peak;
    }

    return underwater_plot;
}


//============================================================================

std::vector<double> get_stats_rolling_drawdown(std::vector<double> const& nlv_history) {
    int n = 252;
    size_t dataSize = nlv_history.size();
    std::vector<double> max_drawdowns(dataSize, 0.0);
    std::deque<int> drawdown_queue;

    for (int i = 0; i < dataSize; ++i) {
        // Remove indices that are outside the current window
        while (!drawdown_queue.empty() && drawdown_queue.front() <= i - n)
            drawdown_queue.pop_front();

        // Remove indices that are not relevant for calculating drawdown
        while (!drawdown_queue.empty() && nlv_history[i] >= nlv_history[drawdown_queue.back()])
            drawdown_queue.pop_back();

        drawdown_queue.push_back(i);

        if (i >= n - 1) {
            max_drawdowns[i] = (nlv_history[drawdown_queue.front()] - nlv_history[i - n + 1])
                / nlv_history[drawdown_queue.front()];
        }
    }
    return max_drawdowns;
}