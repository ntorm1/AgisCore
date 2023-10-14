
#pragma once
#include <cmath>
#include <memory>
#include <stdexcept>
#include "AgisException.h"

#include "Asset/Asset.Base.h"
#include "Asset/Asset.Observer.h"

#define AGIS_EXCEP(msg) \
    AgisException(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + msg)


namespace Agis
{

//============================================================================
void AssetObserver::add_observer() {
    this->asset->add_observer(this);
}


//============================================================================
void AssetObserver::remove_observer() {
    this->asset->remove_observer(this);
}


//============================================================================
std::string AssetObserverTypeToString(AssetObserverType type)
{
    
    switch (type) {
    case AssetObserverType::COL_ROL_MEAN:
        return "COL_ROL_MEAN";
    case AssetObserverType::COL_ROL_VAR:
        return "COL_ROL_VAR";
    case AssetObserverType::COL_ROL_ZSCORE:
        return "COL_ROL_ZSCORE";
    default:
        return "Unknown"; // Return a default value for unknown enums
    }
}


//============================================================================
std::expected<AssetPtr, AgisException>  get_enclosing_asset(
    std::shared_ptr<Asset> a1,
    std::shared_ptr<Asset> a2
)
{
    auto res = a1->encloses(a2);
    if (res.is_exception()) {
        return  std::unexpected<AgisException>(res.get_exception());
    }
    // a1 is the enclosing asset
    if (res.unwrap()) {
        return a1;
    }
    else {
        res = a2->encloses(a1);
        if (res.is_exception()) {
            return  std::unexpected<AgisException>(res.get_exception());
        }
        // a2 is the enclosing asset
        else if (res.unwrap()) {
            return a2;
        }
        // no enclosing asset was fount
        else {
            auto msg = "Assets " + a1->get_asset_id() + " and " + a2->get_asset_id() + " do not enclose each other";
            return  std::unexpected<AgisException>(AGIS_EXCEP(msg));
        }
    }
}


//============================================================================
IncrementalCovariance::IncrementalCovariance(
    std::shared_ptr<Asset> a1,
    std::shared_ptr<Asset> a2) : AssetObserver(a1.get())
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
    auto res = get_enclosing_asset(a1, a2);
    if (!res.has_value()) {
        throw res.error();
    }
    this->enclosing_asset = res.value();
    if (this->enclosing_asset == a1) {
        this->child_asset = a2;
    }
    else {
        this->child_asset = a1;
    }

    // add this to the encosing asset observer list. The enclosing asset must be used as the index into 
    // the enclosing span assumes alignment with the closing span.
    this->enclosing_asset->add_observer(this);
    this->set_asset_ptr(this->enclosing_asset.get());
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
    if (this->index % step_size != 0) {
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
std::string IncrementalCovariance::str_rep() const noexcept
{

    return this->child_asset->get_asset_id() + "_INC_COV_" + std::to_string(this->period);

}


//============================================================================
void IncrementalCovariance::set_pointers(double* upper_triangular_, double* lower_triangular_)
{
    this->upper_triangular = upper_triangular_;
    this->lower_triangular = lower_triangular_;
}



//============================================================================
void MeanVisitor::build() {
    auto col = this->asset->__get_column(this->col_name);
    this->result.clear();
    this->result.resize(col.size());
    double sum = 0;
    for (size_t i = 0; i < col.size(); i++) {
        if (i >= r_count) {
            sum -= col[i - r_count];
        }
        sum += col[i];

        if (i >= r_count - 1) {
            this->result[i] = sum / r_count;
        }
        else {
            this->result[i] = std::numeric_limits<double>::quiet_NaN();
        }
    }
}


//============================================================================
void VarVisitor::build() {
    auto col = this->asset->__get_column(this->col_name);
    this->result.clear();
    this->result.resize(col.size());
    double sum = 0;
    double sos = 0;

    for (size_t i = 0; i < col.size(); i++) {
        if (i >= r_count) {
            sum -= col[i - r_count];
            sos -= col[i - r_count] * col[i - r_count];
        }
        sum += col[i];
        sos += col[i] * col[i];

        if (i >= r_count - 1) {
            double mean = sum / r_count;
            double variance = (sos / (r_count - 1)) - (mean * mean);
            this->result[i] = variance;
        }
        else {
            // Push NaN during the warm-up period
            this->result[i] = std::numeric_limits<double>::quiet_NaN();
        }
    }
}



//============================================================================
void RollingZScoreVisitor::build() {
    mean_visitor.build();
    var_visitor.build();

    const std::vector<double>& mean = mean_visitor.get_result_vec();
    const std::vector<double>& variance = var_visitor.get_result_vec();
    auto col = this->asset->__get_column(this->col_name);
    this->result.clear();
    this->result.resize(mean.size());

    for (size_t i = 0; i < mean.size(); i++) {
        if (!std::isnan(mean[i]) && !std::isnan(variance[i]) && variance[i] > 0) {
            this->result[i] = (col[i] - mean[i]) / std::sqrt(variance[i]);
        }
        else {
            // Push NaN for undefined Z-score values
            this->result[i] = std::numeric_limits<double>::quiet_NaN();
        }
    }
}

//============================================================================
std::expected<AssetObserverPtr, AgisException> create_inc_cov_observer(
    std::shared_ptr<Asset> a1,
    std::shared_ptr<Asset> a2
) {
    std::shared_ptr<AssetObserver> ptr = nullptr;
    try {
        ptr = std::make_shared<IncrementalCovariance>(
            a1,
            a2
        );
    }
    catch (std::exception& e) {
		return std::unexpected<AgisException>(AGIS_EXCEP(e.what()));
	}

    if (!ptr) return std::unexpected<AgisException>(AGIS_EXCEP("Failed to create observer"));
    return ptr;
}


//============================================================================
std::expected<AssetObserverPtr, AgisException> create_roll_col_observer(
    Asset* asset_,
    AssetObserverType type_,
    std::string col_name_,
    size_t r_count_
)
{
    std::shared_ptr<AssetObserver> ptr = nullptr;
    try {
        switch (type_) {
        case AssetObserverType::COL_ROL_MEAN:
            ptr = std::make_shared<MeanVisitor>(
                asset_,
                col_name_,
                r_count_
            );
            break;
        default:
            break;
        }
    }
    catch (std::exception& e) {
        return std::unexpected<AgisException>(AGIS_EXCEP(e.what()));
    }
    if (!ptr) return std::unexpected<AgisException>(AGIS_EXCEP("Failed to create observer"));
    return ptr;
}

}