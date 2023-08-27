#include "pch.h"
#include "AgisRisk.h"


//============================================================================
double covariance(const std::vector<double>& values1, const std::vector<double>& values2, size_t start, size_t end)
{
    double sum = 0.0;
    size_t n = end - start;

    for (size_t i = start; i < end; i++)
    {
        sum += (values1[i] - mean(values1, start, end)) * (values2[i] - mean(values2, start, end));
    }

    return sum / static_cast<double>(n - 1);
}


//============================================================================
double variance(const std::vector<double>& values, size_t start, size_t end)
{
    double sum = 0.0;
    size_t n = end - start;

    for (size_t i = start; i < end; i++)
    {
        sum += std::pow(values[i] - mean(values, start, end), 2);
    }

    return sum / static_cast<double>(n - 1);
}


//============================================================================
double mean(const std::vector<double>& values, size_t start, size_t end)
{
    double sum = 0.0;

    for (size_t i = start; i < end; i++)
    {
        sum += values[i];
    }

    return sum / static_cast<double>(end - start);
}


//============================================================================
double correlation(const std::vector<double>& values1, const std::vector<double>& values2, size_t start, size_t end)
{
    double cov = covariance(values1, values2, start, end);
    double var1 = variance(values1, start, end);
    double var2 = variance(values2, start, end);

    return cov / (std::sqrt(var1) * std::sqrt(var2));
}


//============================================================================
std::vector<double> rolling_beta(const std::vector<double>& stock_returns, const std::vector<double>& market_returns, size_t window_size)
{
    // works for now. Slightly off from pands rolling beta, probably ddof difference
    // matches python: 
    //cov = sum(df_mid["returns_SPY"].head(252) * df_mid[f"returns_{ticker}"].head(252))
    //var = sum(df_mid["returns_SPY"].head(252) * df_mid["returns_SPY"].head(252))
    //beta= cov/var

    size_t data_size = stock_returns.size();

    // beta is calculated with returns, to make sure the beta vector is the same length
    // as the the asset's row count, insert 0 at the beginning
    std::vector<double> rolling_betas; // Initialize with zeros
    rolling_betas.reserve(data_size + 1);
    rolling_betas.push_back(0.0);

    double rolling_covariance = 0.0;
    double rolling_market_variance = 0.0;

    // Calculate initial covariance and market variance
    for (size_t i = 0; i < window_size; ++i) {
        rolling_covariance += stock_returns[i] * market_returns[i];
        rolling_market_variance += market_returns[i] * market_returns[i];
        rolling_betas.push_back(0.0f);
    }

    // note, note window_size - 1 because of the fact that we are using returns, 
    // the first real element will be at the index location: window_size
    rolling_betas[window_size] = rolling_covariance / rolling_market_variance;

    // Update covariance and variance incrementally
    for (size_t i = window_size; i < data_size; ++i) {
        double old_stock_return = stock_returns[i - window_size];
        double old_market_return = market_returns[i - window_size];

        double new_stock_return = stock_returns[i];
        double new_market_return = market_returns[i];

        rolling_covariance += (new_stock_return * new_market_return - old_stock_return * old_market_return);
        rolling_market_variance += (new_market_return * new_market_return - old_market_return * old_market_return);

        rolling_betas.push_back(rolling_covariance / rolling_market_variance);
    }

    return rolling_betas;
}