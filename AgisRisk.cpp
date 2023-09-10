#include "pch.h"
#include "AgisRisk.h"
#include "Asset.h"
#include "Order.h"
#include "Exchange.h"
#include "AgisStrategy.h"


size_t IncrementalCovariance::step_size(1);
size_t IncrementalCovariance::period(0);


constexpr auto SQRT_252 = 15.874507866387544;


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
std::vector<double> rolling_volatility(std::span<double> const prices, size_t window_size)
{
    std::vector<double> rolling_volatility; // Initialize with zeros

    if (prices.size() < window_size) {
        // Handle the case where the window size is greater than the number of returns.
        // You may want to return an error or handle it differently based on your requirements.
        return rolling_volatility;
    }

    // volatility is calculated with returns, to make sure the vol vector is the same length
    // as the the asset's row count, insert 0 at the beginning
    rolling_volatility.reserve(prices.size());
    rolling_volatility.push_back(0.0);

    double sum = 0.0;
    double sos = 0.0;

    // Calculate the initial sums for the first window
    for (size_t i = 1; i < window_size; ++i) {
        double returnVal = (prices[i] - prices[i - 1]) / prices[i - 1];
        sum += returnVal;
        sos += returnVal * returnVal;
        rolling_volatility.push_back(0.0f);
    }

    for (size_t i = window_size; i < prices.size(); ++i) {
        // Calculate the rolling standard deviation (volatility) for the current window.
        double returnVal = (prices[i] - prices[i - 1]) / prices[i - 1];
        sum += returnVal;
        sos += returnVal * returnVal;
        double mean = sum / (window_size);
        double variance = (sos / (window_size-1)) - (mean * mean);
        double volatility = std::sqrt(variance) * SQRT_252;

        rolling_volatility.push_back(volatility);

        if (i > window_size) {
            double old_return = (prices[i - window_size] - prices[i - window_size - 1]) / prices[i - window_size - 1];
            sum -= old_return;
            sos -= old_return * old_return;
        }
    }

    return rolling_volatility;
}



//============================================================================
IncrementalCovariance::IncrementalCovariance(
    std::shared_ptr<Asset> a1,
    std::shared_ptr<Asset> a2)
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

    a1->__set_warmup(period * step_size);
    a2->__set_warmup(period * step_size);

    // find the enclosing asset. One aset most enclose the other i.e. the datetimeindex of the child 
    // must be a subset of the enclosing asset and have no gaps
    std::shared_ptr<Asset> enclosing_asset;
    std::shared_ptr<Asset> child_asset;
    auto res = a1->encloses(a2);
    if (res.is_exception()) throw res.get_exception();
    // a1 is the enclosing asset
    if (res.unwrap()) {
        enclosing_asset = a1;
        child_asset = a2;
    }
    else {
        res = a2->encloses(a1);
        if (res.is_exception()) throw res.get_exception();
        // a2 is the enclosing asset
        else if (res.unwrap()) {
            enclosing_asset = a2;
            child_asset = a1;
        }
        // no enclosing asset was fount
        else {
            auto msg = "Assets " + a1->get_asset_id() + " and " + a2->get_asset_id() + " do not enclose each other";
            throw std::runtime_error(msg);
        }
    }
    // add this to the encosing asset observer list. The enclosing asset must be used as the index into 
    // the enclosing span assumes alignment with the closing span.
    enclosing_asset->add_observer(this);

    this->enclosing_span = enclosing_asset->__get_column((enclosing_asset->__get_close_index()));
    this->child_span = child_asset->__get_column((child_asset->__get_close_index()));
    this->enclosing_span_start_index = enclosing_asset->encloses_index(child_asset).unwrap();
}


//============================================================================
void IncrementalCovariance::on_step()
{
    // if the current index is less than the enclosing span start index then return, not in the time period in which
    // the two assets have overlapping data
    if (this->index <= this->enclosing_span_start_index || this->index < step_size) {
        this->index++;
        return;
    }

    // check if on step
    if(this->index % step_size != 0) {
        this->index++;
		return;
	}

    auto child_index = index - this->enclosing_span_start_index;
    double enclose_pct_change = (enclosing_span[index] - enclosing_span[index - step_size]) / enclosing_span[index - step_size];
    double child_pct_change = (child_span[child_index] - child_span[child_index - step_size]) / child_span[child_index - step_size];
    this->sum1 += enclose_pct_change;
    this->sum2 += child_pct_change;
    this->sum_product += enclose_pct_change * child_pct_change;
    this->sum1_squared += enclose_pct_change * enclose_pct_change;
    this->sum2_squared += child_pct_change * child_pct_change;

    // check if in warmup
    if (child_index < this->period * this->step_size) {
        this->index++;
        return;
    }

    // check if need to remove the previous value
    if (child_index > this->period) {
        size_t child_index_prev = (child_index - 1) - (this->period * this->step_size - this->step_size);
        size_t index_prev = (index - 1) - (this->period * this->step_size - this->step_size);
        enclose_pct_change = (enclosing_span[index_prev] - enclosing_span[index_prev - step_size]) / enclosing_span[index_prev - step_size];
        child_pct_change = (child_span[child_index_prev] - child_span[child_index_prev - step_size]) / child_span[child_index_prev - step_size];
        this->sum1 -= enclose_pct_change;
        this->sum2 -= child_pct_change;
        this->sum_product -= enclose_pct_change * child_pct_change;
        this->sum1_squared -= enclose_pct_change * enclose_pct_change;
        this->sum2_squared -= child_pct_change * child_pct_change;
    }

    // set covariance and matrix pointers
    this->covariance = (sum_product - sum1 * sum2 / period) / (period - 1);
    *this->upper_triangular = this->covariance;
    *this->lower_triangular = this->covariance;

    this->index++;
}


//============================================================================
void IncrementalCovariance::on_reset()
{
    this->sum1 = 0.0;
	this->sum2 = 0.0;
	this->sum_product = 0.0;
	this->sum1_squared = 0.0;
	this->sum2_squared = 0.0;
	this->covariance = 0.0;
	this->index = 0;
    *this->lower_triangular = 0.0f;
    *this->upper_triangular = 0.0f;
}


//============================================================================
void IncrementalCovariance::set_pointers(double* upper_triangular_, double* lower_triangular_)
{
    this->upper_triangular = upper_triangular_;
	this->lower_triangular = lower_triangular_;
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
    if (order->has_beta_hedge_order()) {
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


//============================================================================
AgisCovarianceMatrix::AgisCovarianceMatrix(
    ExchangeMap* exchange_map,
    size_t lookback_,
    size_t step_size_
)
{
    this->lookback = lookback_;
    this->step_size = step_size_;

    IncrementalCovariance::step_size = this->step_size;
    IncrementalCovariance::period = this->lookback;

    auto& assets = exchange_map->get_assets();
    auto asset_count = assets.size();
    this->covariance_matrix.resize(asset_count, asset_count);
    this->covariance_matrix.setZero();

    // build the incremental covariance matrix
    for (size_t i = 0; i < asset_count; i++)
    {
        for (size_t j = 0; j < i + 1; j++)
        {
            if (assets[i]->get_rows() <= lookback || assets[j]->get_rows() <= lookback) continue;
            std::shared_ptr<IncrementalCovariance> incremental_covariance;
            try {
                incremental_covariance = std::make_shared<IncrementalCovariance>(
                    assets[i],
                    assets[j]
                );
            }
            catch (std::runtime_error& e) {
			    throw e;
			}
            auto upper_triangular = &this->covariance_matrix(i, j);
            auto lower_triangular = &this->covariance_matrix(j, i);
            incremental_covariance->set_pointers(upper_triangular, lower_triangular);
            incremental_covariance_matrix.push_back(incremental_covariance);
		}
    }
}
