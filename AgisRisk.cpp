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
std::vector<double> rolling_beta(const std::vector<double>& stockReturns, const std::vector<double>& marketReturns, size_t windowSize)
{
    int dataSize = stockReturns.size();
    std::vector<double> rollingBetas(dataSize, 0.0); // Initialize with zeros

    double rollingCovariance = 0.0;
    double rollingMarketVariance = 0.0;

    // Calculate initial covariance and market variance
    for (size_t i = 0; i < windowSize; ++i) {
        rollingCovariance += stockReturns[i] * marketReturns[i];
        rollingMarketVariance += marketReturns[i] * marketReturns[i];
    }

    rollingBetas[windowSize - 1] = rollingCovariance / rollingMarketVariance;

    // Update covariance and variance incrementally
    for (size_t i = windowSize; i < dataSize; ++i) {
        double oldStockReturn = stockReturns[i - windowSize];
        double oldMarketReturn = marketReturns[i - windowSize];

        double newStockReturn = stockReturns[i];
        double newMarketReturn = marketReturns[i];

        rollingCovariance += (newStockReturn * newMarketReturn - oldStockReturn * oldMarketReturn);
        rollingMarketVariance += (newMarketReturn * newMarketReturn - oldMarketReturn * oldMarketReturn);

        rollingBetas[i] = rollingCovariance / rollingMarketVariance;
    }

    return rollingBetas;
}