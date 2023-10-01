#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"
#include <span>
#include <unordered_map>

class Asset;
class AssetObserver;
class IncrementalCovariance;

typedef std::shared_ptr<Asset> AssetPtr;
typedef std::shared_ptr<AssetObserver> AssetObserverPtr;


//============================================================================
enum class AGIS_API AssetObserverType {
	COL_ROL_MEAN,
	COL_ROL_VAR,
	COL_ROL_ZSCORE,
};


//============================================================================
static std::unordered_map<AssetObserverType, std::string> AssetObserverTypeMap = {
	{ AssetObserverType::COL_ROL_MEAN, "COL_ROL_MEAN" },
	{ AssetObserverType::COL_ROL_VAR, "COL_ROL_VAR" },
	{ AssetObserverType::COL_ROL_ZSCORE, "COL_ROL_ZSCORE" },
};


//============================================================================
class AssetObserver {
public:
    virtual ~AssetObserver() {}
    AssetObserver(NonNullRawPtr<Asset> asset_) : asset(asset_) {}
    AssetObserver(Asset* asset_) : asset(asset_) {}

    virtual void on_step() = 0;
    virtual void on_reset() = 0;
	virtual inline double get_result() const noexcept = 0;
	bool get_touch() const noexcept { return this->touch; }
	size_t get_warmup() const noexcept { return this->warmup; }
	NonNullRawPtr<Asset> get_asset_ptr() const noexcept { return this->asset; }

	auto get_result_func() const noexcept {
		return [this]() { return this->get_result(); };
	}
	virtual inline std::string str_rep() const noexcept = 0;

	void set_touch(bool t) { this->touch = t; }

protected:
    void set_asset_ptr(Asset* asset_) { this->asset = asset_; }
	void add_observer();
	void remove_observer();
	void set_warmup(size_t w) { this->warmup = w; }

	NonNullRawPtr<Asset> asset;
	size_t warmup = 0;

private:
	/**
	 * @brief when an observer is created by a strategy it is added to the exchange. However
	 * if no strategy attempts to create it when build method is called it will be removed
	*/
	bool touch = true;
};


//============================================================================
/**
 * @brief base class for all column observers that compute their entire column on build
 * then index into it on step
*/
class DataFrameColObserver : public AssetObserver {
public:
	virtual ~DataFrameColObserver() {}

	DataFrameColObserver(
		Asset* asset_,
		AssetObserverType type_
	): 
		AssetObserver(asset_),
		observer_type(type_)
	{}

	/**
	 * @brief pure virtual call used to build the visitor column on init (only call one)
	*/
	virtual void build() = 0;

	/**
	 * @brief on asset rest move the index to start and build if needed
	*/
	void on_reset() override {
		if (!this->is_built) {
			this->build();
		}
		this->index = 0;
	}

	/**
	 * @brief on asset step increment the index
	*/
	void on_step() override {
		this->index++;
	}

	/**
	 * @brief accessor for the visitor index column
	 * @return 
	*/
	inline double get_result() const noexcept override {
		assert(this->index - 1 <= this->result.size());
		return this->result[this->index - 1];
	}

	inline auto const& get_result_vec() const noexcept {
		return this->result;
	}

protected:
	std::vector<double> result;
	AssetObserverType observer_type;

private:
	bool is_built = false;
	size_t index = 0;
};


//============================================================================
class MeanVisitor : public DataFrameColObserver
{
public:
	MeanVisitor() = default;
	MeanVisitor(
		Asset* asset_,
		std::string col_name_,
		size_t r_count_
	) : 
		r_count(r_count_),	
		DataFrameColObserver(asset_, AssetObserverType::COL_ROL_MEAN)
	{
		this->col_name = col_name_;
		this->set_warmup(r_count);
	}

	void build() override;

	std::string str_rep() const noexcept override {
		return col_name + "_" + AssetObserverTypeMap.at(this->observer_type) + "_" + std::to_string(this->r_count);
	}

private:
	std::string col_name;
	size_t r_count;
};


//============================================================================
class VarVisitor : public DataFrameColObserver
{
public:
	VarVisitor() = default;
	VarVisitor(
		Asset* asset_,
		std::string col_name_,
		size_t r_count_
	) :
		r_count(r_count_),
		DataFrameColObserver(asset_, AssetObserverType::COL_ROL_VAR)
	{
		this->col_name = col_name_;
		this->set_warmup(r_count);
	}

	void build() override;

	std::string str_rep() const noexcept override {
		return col_name + "_" + AssetObserverTypeMap.at(this->observer_type) + "_" + std::to_string(this->r_count);
	}

private:
	std::string col_name;
	size_t r_count;
};


//============================================================================
class RollingZScoreVisitor : public DataFrameColObserver {
public:
	RollingZScoreVisitor() = default;
	RollingZScoreVisitor(
		Asset* asset_,
		std::string col_name_,
		size_t r_count_
	) :
		r_count(r_count_),
		DataFrameColObserver(asset_, AssetObserverType::COL_ROL_ZSCORE),
		mean_visitor(asset_, col_name_, r_count_),
		var_visitor(asset_, col_name_, r_count_)
	{
		this->col_name = col_name_;
		this->set_warmup(r_count);
	}

	void build() override;

	std::string str_rep() const noexcept override {
		return col_name + "_" + AssetObserverTypeMap.at(this->observer_type) + "_" + std::to_string(this->r_count) + "_ZScore";
	}

private:
	std::string col_name;
	size_t r_count;
	MeanVisitor mean_visitor;
	VarVisitor var_visitor;
};


//============================================================================
class IncrementalCovariance : public AssetObserver
{
	friend struct AgisCovarianceMatrix;
public:
	IncrementalCovariance(
		std::shared_ptr<Asset> a1,
		std::shared_ptr<Asset> a2
	);

	static size_t step_size;
	static size_t period;

	/**
	 * @brief set the pointers into the covariance matrix that this incremental covariance struct will update
	 * @param upper_triangular_ pointer to the upper triangular portion of the covariance matrix
	 * @param lower_triangular_ pointer to the lower triangular portion of the covariance matrix
	*/
	void set_pointers(double* upper_triangular_, double* lower_triangular_);

	/**
	 * @brief function called on step of exchange to update this incremental covariance struct
	*/
	void on_step() override;

	/**
	 * @brief function called on reset of exchange to reset this incremental covariance struct
	*/
	void on_reset() override;

	/**
	 * @brief get the string representation of this incremental covariance struct
	 * @return the string representation of this incremental covariance struct
	*/
	std::string str_rep() const noexcept override;

	/**
	 * @brief get the current covariance value
	 * @return the current covariance value
	*/
	inline double get_result() const noexcept override
	{
		return covariance;
	}

private:
	AssetPtr enclosing_asset = nullptr;
	AssetPtr child_asset = nullptr;
	std::span<double const> enclosing_span;
	std::span<double const> child_span;
	size_t enclosing_span_start_index;
	size_t index = 0;
	double sum1 = 0;
	double sum2 = 0;
	double sum_product = 0;
	double sum1_squared = 0;
	double sum2_squared = 0;
	double covariance = 0;

	double* upper_triangular = nullptr;
	double* lower_triangular = nullptr;
};




//============================================================================
AGIS_API AgisResult<AssetObserverPtr> create_inc_cov_observer(
	std::shared_ptr<Asset> a1,
	std::shared_ptr<Asset> a2
);


#ifdef USE_DATAFRAME
//============================================================================
AGIS_API AgisResult<AssetObserverPtr> create_roll_col_observer(
	Asset* asset_,
	AssetObserverType type_,
	std::string col_name_,
	size_t r_count_
);
#endif