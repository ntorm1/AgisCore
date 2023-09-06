#include "pch.h"
#include "AgisRisk.h"
#include "Asset.h"
#include "Order.h"
#include "AgisStrategy.h"

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
double mean(const double* values, size_t start, size_t end)
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



//============================================================================
IncrementalCovariance::IncrementalCovariance(
    std::shared_ptr<Asset> a1,
    std::shared_ptr<Asset> a2,
    size_t period) : IncrementalCovariance(
        a1->__get_column(a1->__get_close_index()),
        a2->__get_column(a2->__get_close_index()),
        period,
        a1->get_current_index()
    )
{
#ifdef _DEBUG
    if (a1->get_frequency() != a2->get_frequency())
    {
        throw std::runtime_error("Assets must have the same frequency");
    }
    if (a1->get_current_index() != a2->get_current_index())
    {
        throw std::runtime_error("Assets must have the same current index");
    }
#endif
}


//============================================================================
void AgisRiskStruct::__build(AgisStrategy const* parent_strategy_)
{
    this->parent_strategy = parent_strategy_;
    this->exchange_map = parent_strategy->get_exchanges();
    auto asset_count = parent_strategy->get_exchanges()->get_asset_count();
    this->asset_holdings.resize(asset_count, 0.0);
}


//============================================================================
void AgisRiskStruct::__reset()
{
    for (size_t i = 0; i < this->asset_holdings.size(); i++)
    {
        this->asset_holdings[i] = 0.0f;
    }
}


//============================================================================
double AgisRiskStruct::estimate_phantom_cash(Order const* order)
{
    double cash_estimate = 0.0f;
    auto asset_index = order->get_asset_index();
    double order_units = order->get_units();
    auto market_price = this->exchange_map->__get_market_price(asset_index, true);

    // check to see if the order will increase or reduce a position
    if (this->asset_holdings[asset_index] * order_units >= 0)
    {
        cash_estimate = abs(order_units) * market_price;
    }
    else
    {
        cash_estimate = (order_units)*market_price;
    }

    // get cash required for child orders
    if (order->has_child_order()) {
        auto& child_order_ref = order->get_child_order_ref();
        asset_index = child_order_ref->get_asset_index();
        market_price = this->exchange_map->__get_market_price(child_order_ref->get_asset_index(), true);
        if (this->asset_holdings[asset_index] * order_units >= 0)
        {
            cash_estimate += abs(child_order_ref->get_units()) * market_price;
        }
        else
        {
            cash_estimate += (child_order_ref->get_units()) * market_price;
        }
    }
    return cash_estimate;
}




