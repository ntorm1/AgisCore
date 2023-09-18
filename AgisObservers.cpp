#include "pch.h"
#include "AgisObservers.h"
#include "Asset.h"

size_t IncrementalCovariance::step_size(1);
size_t IncrementalCovariance::period(0);


//============================================================================
void AssetObserver::add_observer() {
    this->asset->add_observer(this);
}


//============================================================================
void AssetObserver::remove_observer() {
    this->asset->remove_observer(this);
}


//============================================================================
AgisResult<AssetPtr> get_enclosing_asset(
    std::shared_ptr<Asset> a1,
    std::shared_ptr<Asset> a2
)
{
    auto res = a1->encloses(a2);
    if (res.is_exception()) {
        return AgisResult<AssetPtr>(res.get_exception());
    }
    // a1 is the enclosing asset
    if (res.unwrap()) {
        return AgisResult<AssetPtr>(a1);
    }
    else {
        res = a2->encloses(a1);
        if (res.is_exception()) {
            return AgisResult<AssetPtr>(res.get_exception());
        }
        // a2 is the enclosing asset
        else if (res.unwrap()) {
            return AgisResult<AssetPtr>(a2);
        }
        // no enclosing asset was fount
        else {
            auto msg = "Assets " + a1->get_asset_id() + " and " + a2->get_asset_id() + " do not enclose each other";
            return AgisResult<AssetPtr>(AGIS_EXCEP(msg));
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
    if (res.is_exception()) {
        throw res.get_exception();
    }
    this->enclosing_asset = res.unwrap();
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
AGIS_API AgisResult<AssetObserverPtr> create_inc_cov_observer(
    std::shared_ptr<Asset> a1,
    std::shared_ptr<Asset> a2
) {
    std::shared_ptr<AssetObserver> ptr = nullptr;
    AGIS_TRY_RESULT(ptr = std::make_shared<IncrementalCovariance>(
        a1,
        a2
    ), AssetObserverPtr);
    if (!ptr) return AgisResult<AssetObserverPtr>(AGIS_EXCEP("Failed to create observer"));
    return AgisResult<AssetObserverPtr>(ptr);
}


#ifdef USE_DATAFRAME
//============================================================================
AGIS_API AgisResult<AssetObserverPtr> create_roll_col_observer(
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
            ptr = std::make_shared<RollingVisitor<MeanVisitor<double, long long>>>(
                asset_,
                col_name_,
                r_count_,
                AssetObserverType::COL_ROL_MEAN
            );
            break;
        default:
            break;
        }
    }
    catch (std::exception& e) {
		return AgisResult<AssetObserverPtr>(AGIS_EXCEP(e.what()));
	}
    if (!ptr) return AgisResult<AssetObserverPtr>(AGIS_EXCEP("Failed to create observer"));
    return AgisResult<AssetObserverPtr>(ptr);
}
#endif