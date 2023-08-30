#include "pch.h"
#include "AgisAnalysis.h"
#include "AgisStrategy.h"
#include "Portfolio.h"
#include <deque>


//============================================================================
PortfolioStats::PortfolioStats(Portfolio* portfolio_, double cash_, double risk_free_) :
    entity(portfolio_)
{
	this->risk_free = risk_free_;
    this->starting_cash = cash_;
    this->cash = cash_;
    this->nlv = cash_;
}


//============================================================================
PortfolioStats::PortfolioStats(AgisStrategy* strategy_,double cash_, double risk_free_) :
    entity(strategy_)
{
    this->risk_free = risk_free_;
    this->starting_cash = cash_;
    this->cash = cash_;
    this->nlv = cash_;
}


//============================================================================
void PortfolioStats::__reserve(size_t n)
{
    this->nlv_history.reserve(n);
    this->cash_history.reserve(n);
    if(this->is_beta_tracing) this->beta_history.reserve(n);
}

//============================================================================

void PortfolioStats::__reset()
{
    this->cash_history.clear();
    this->nlv_history.clear();
    this->beta_history.clear();

    this->cash = this->starting_cash;
    this->nlv = this->cash;
    this->net_beta = 0.0f;
}


//============================================================================
void PortfolioStats::__evaluate()
{
    this->nlv_history.push_back(this->nlv);
    this->cash_history.push_back(this->cash);
    if (this->is_beta_tracing) this->beta_history.push_back(this->net_beta);
}

//============================================================================
double PortfolioStats::get_stats_total_pl() const
{
	return this->nlv_history.back() - this->nlv_history.front();
}


//============================================================================
double PortfolioStats::get_stats_pct_returns() const
{
	return 100 * this->get_stats_total_pl() / this->nlv_history.front();
}


//============================================================================
double PortfolioStats::get_stats_annualized_pct_returns() const
{
	// calculate annualized returns
	auto days = this->nlv_history.size();
	auto years = days / 252.0;
	return 100 * (pow(nlv_history.back() / nlv_history.front(), 1 / years) - 1);
}


//============================================================================
double PortfolioStats::get_stats_annualized_volatility() const
{
	// calculate annualized volatility
	auto days = this->nlv_history.size();
	auto years = days / 252.0;
	auto daily_returns = std::vector<double>(days - 1);
	for (size_t i = 0; i < days - 1; i++)
	{
		daily_returns[i] = (this->nlv_history[i + 1] - this->nlv_history[i]) / this->nlv_history[i];
	}
	auto mean = std::accumulate(daily_returns.begin(), daily_returns.end(), 0.0) / daily_returns.size();
	auto sq_sum = std::inner_product(daily_returns.begin(), daily_returns.end(), daily_returns.begin(), 0.0);
	auto stdev = std::sqrt(sq_sum / daily_returns.size() - mean * mean);
	return 100 * stdev * std::sqrt(252);
}


//============================================================================
double PortfolioStats::get_stats_sharpe_ratio() const
{
	// Calculate the Sharpe Ratio
	return (get_stats_annualized_pct_returns() - risk_free) 
        / get_stats_annualized_volatility();
}


//============================================================================
Drawdown PortfolioStats::get_stats_drawdown() const
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
std::vector<double> PortfolioStats::get_rolling_sharpe(size_t window_size)const
{
    std::vector<double> returns;
    std::vector<double> rolling_sharpe_ratios;

    double rolling_avg_return = 0.0;
    double rolling_std_dev_sum = 0.0;

    for (size_t i = 1; i <= this->nlv_history.size(); ++i) {
        double current_return = (this->nlv_history[i] - this->nlv_history[i - 1]) / this->nlv_history[i - 1];
        returns.push_back(current_return);

        rolling_avg_return += current_return;
        rolling_std_dev_sum += current_return * current_return;

        if (i >= window_size) {
            if (i > window_size) {
                double removed_return = (this->nlv_history[i - window_size] - this->nlv_history[i - window_size - 1]) 
                    / this->nlv_history[i - window_size - 1];
                rolling_avg_return -= removed_return;
                rolling_std_dev_sum -= removed_return * removed_return;
            }

            double avg_return = rolling_avg_return / window_size;
            double std_dev = std::sqrt((rolling_std_dev_sum - window_size * avg_return * avg_return) / (window_size - 1));

            double annualized_avg_return = avg_return * 252; // Assuming 252 trading days in a year
            double annualized_std_dev = std_dev * std::sqrt(252); // Annualize standard deviation

            double sharpe_ratio = (annualized_avg_return - this->risk_free) / annualized_std_dev;           
            rolling_sharpe_ratios.push_back(sharpe_ratio);
        }
    }
    return rolling_sharpe_ratios;
}


//============================================================================
std::vector<double> PortfolioStats::get_stats_underwater_plot() const{
    auto n = this->nlv_history.size();
    std::vector<double> underwater_plot(n, 0.0);

    double peak = this->nlv_history[0];
    for (size_t i = 0; i < n; ++i) {
        if (this->nlv_history[i] > peak) {
            peak = this->nlv_history[i];
        }
        underwater_plot[i] = (this->nlv_history[i] - peak) / peak;
    }

    return underwater_plot;
}


//============================================================================
std::vector<double> PortfolioStats::get_stats_rolling_drawdown() const {
    int n = 252;
    int dataSize = this->nlv_history.size();
    std::vector<double> max_drawdowns(dataSize, 0.0);
    std::deque<int> drawdown_queue;

    for (int i = 0; i < dataSize; ++i) {
        // Remove indices that are outside the current window
        while (!drawdown_queue.empty() && drawdown_queue.front() <= i - n)
            drawdown_queue.pop_front();

        // Remove indices that are not relevant for calculating drawdown
        while (!drawdown_queue.empty() && this->nlv_history[i] >= this->nlv_history[drawdown_queue.back()])
            drawdown_queue.pop_back();

        drawdown_queue.push_back(i);

        if (i >= n - 1) {
            max_drawdowns[i] = (this->nlv_history[drawdown_queue.front()] - this->nlv_history[i - n + 1]) 
                / this->nlv_history[drawdown_queue.front()];
        }
    }
    return max_drawdowns;
}