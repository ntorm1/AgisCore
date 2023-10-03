#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h" 

#include <ankerl/unordered_dense.h>

#include "AgisRisk.h"

class Exchange;
class ExchangeMap;

struct Trade;
typedef std::shared_ptr<Trade> SharedTradePtr;

#define CHECK_INDEX_MATCH(lhs, rhs) \
    do { \
        if ((lhs).exchange != (rhs).exchange) { \
            throw std::runtime_error("index mismatch"); \
        } \
    } while (false)

#define CHECK_SIZE_MATCH(lhs, rhs) \
    do { \
        if ((lhs).size() != (rhs).size()) { \
            throw std::runtime_error("size mismatch"); \
        } \
    } while (false)

/// <summary>
/// Type of exchange query to make, used when access a column for every asset on the exchange
/// </summary>
enum ExchangeQueryType
{
	Default,	/// return all assets in view
	NLargest,	/// return the N largest
	NSmallest,	/// return the N smallest
	NExtreme	/// return the N/2 smallest and largest
};


enum class ExchangeViewOpp
{
	UNIFORM,			/// applies 1/N weight to each pair
	LINEAR_DECREASE,	/// applies a linear decrease in weight from 1 to 0
	LINEAR_INCREASE,	/// applies a linear increase in weight from 0 to 1
	CONDITIONAL_SPLIT,	/// -1/N for values below c, 1/N for values above c
	UNIFORM_SPLIT,		/// -1/N for first N/2 , 1/N for last N/2
	CONSTANT			/// applies a constant weight to each pair
};


AGIS_API extern std::vector<std::string> exchange_view_opps;

AGIS_API std::string ev_opp_to_str(ExchangeViewOpp ev_opp);
AGIS_API std::string ev_query_type(ExchangeQueryType ev_query);

struct ExchangeViewAllocation {
	ExchangeViewAllocation() = default;
	ExchangeViewAllocation(size_t asset_index_, double allocation_amount_) {
		this->asset_index = asset_index_;
		this->allocation_amount = allocation_amount_;
	}
	/**
	 * @brief unique if if of the asset
	*/
	size_t asset_index;

	/**
	 * @brief size of the allocation
	*/
	double allocation_amount = 0.0f;

	/**
	 * @brief beta of the asset
	*/
	std::optional<double> beta = 0.0f;

	/**
	 * @brief optional beta hedge size linked to the allocation. As of now beta hedges are applied at the trade level instead of the portfolio level. 
	 * This allows for more flexibility, i.e. if some order is rejected than the beta hedge is still correct.
	*/
	std::optional<double> beta_hedge_size = std::nullopt;

	/**
	* @brief wether or not the allocation has been touched by the strategy, useful for reusing evs
	*/
	bool live = false;
};

struct ExchangeView
{
	std::vector<ExchangeViewAllocation> view;
	std::optional<double> market_asset_price = std::nullopt;
	std::optional<size_t> market_asset_index = std::nullopt;
	const Exchange* exchange = nullptr;

	AGIS_API ExchangeView() = default;
	AGIS_API ExchangeView(const Exchange* exchange_, size_t count, bool reserve = true);

	/// <summary>
	/// Return the number of elements in the exchange view
	/// </summary>
	/// <returns></returns>
	size_t size() const { return this->view.size(); }

	/**
	 * @brief remove a specific allocation if it exists in the exchange view
	 * @param asset_index unique id of the asset allocation to remove 
	*/
	void remove_allocation(size_t asset_index);

	/**
	 * @brief removes all allocations that have live set to false, does not preserver ordering
	 * @return 
	*/
	AGIS_API void clean();

	/// <summary>
	/// Take an exchange view, then sort and extract a subset of the view
	/// </summary>
	/// <param name="N">number of elements to retunr</param>
	/// <param name="sort_type">type of sort to do</param>
	AGIS_API void sort(size_t N, ExchangeQueryType sort_type);

	/// <summary>
	/// Sum of all weights in the exchange view
	/// </summary>
	/// <returns></returns>
	AGIS_API double sum_weights(bool _abs = false, bool include_beta_hedge = false) const;

	/// <summary>
	/// Get the neta beta of the exchange view allocation
	/// </summary>
	/// <returns></returns>
	AGIS_API AgisResult<double> net_beta() const;

	/// <summary>
	/// Divide each pairs weights by it's respective beta, while maintaing the same leverage
	/// Note: designed for use with percentage weights.
	/// </summary>
	/// <param name="exchange_map"></param>
	/// <returns></returns>
	AGIS_API AgisResult<bool> beta_scale();

	/**
	 * @brief takes an exchange view with current portfolio allocations and scales it 
	 * to target a specific level of volatility using the main exchange map covaraince matrix
	 * @return 
	*/
	AGIS_API AgisResult<bool> vol_target(double target);

	/**
	 * @brief takes a portfolio allocation in terms of percentage of nlv and scales it to target a specific level
	 * of overall portfolio volatility using the main exchange map covaraince matrix
	 * @param agis_cov_matrix const ref to an agis covariance matrix
	 * @return result of operations
	*/
	AGIS_API AgisResult<bool> apply_vol_target(std::shared_ptr<AgisCovarianceMatrix> const agis_cov_matrix);

	/// <summary>
	/// Generate a beta hedge for the portfolio and adjust allocation weights to match 
	/// the target leverage of the portfolio. Note assumes the allocations are % of nlv
	/// </summary>
	/// <param name="target_leverage">the target leverage of the strategy, otherwise maintains allocation leverage</param>
	/// <returns></returns>
	AGIS_API AgisResult<bool> beta_hedge(std::optional<double> target_leverage);

	/// <summary>
	/// set the weights of all pairs equal to c
	/// </summary>
	AGIS_API void realloc(double c);


	/// <summary>
	/// Take an exchange view, then sort the pairs based on the second element in the pair
	/// </summary>
	void sort_pairs() {
		// sort the view based on the second argument in the pair
		std::sort(this->view.begin(), this->view.end(), [](auto const& lhs, auto const& rhs) {
			return lhs.allocation_amount < rhs.allocation_amount;
			});
	}

	void apply_weights(
		std::string const& type,
		double c,
		std::optional<double> x = std::nullopt)
	{
		if (type == "UNIFORM") this->uniform_weights(c);
		else if (type == "LINEAR_DECREASE") this->linear_decreasing_weights(c);
		else if (type == "LINEAR_INCREASE") this->linear_increasing_weights(c);
		else if (type == "CONDITIONAL_SPLIT") this->conditional_split(c, x.value());
		else AGIS_THROW("invalid weight function name");
	};

	/// <summary>
	/// Apply a single weight to every value in the exchange view
	/// </summary>
	/// <param name="c">target leverage</param>
	void uniform_weights(double c) {
		auto weight = c / static_cast<double>(view.size());
		for (auto& pair : view) {
			pair.allocation_amount = weight;
		}
	}

	/**
	 * @brief Applies a constant weight to every value in the exchange view
	 * @param c constant weight to be applied
	*/
	void constant_weights(double c, ankerl::unordered_dense::map<size_t, SharedTradePtr> const& trades){
		// remove all trades that are in the trades map
		auto shouldRemove = [&](ExchangeViewAllocation const& element) {
			return trades.contains(element.asset_index);
		};
		view.erase(std::remove_if(view.begin(), view.end(), shouldRemove), view.end());
		for (auto& pair : view) {
			pair.allocation_amount = c;
		}
	}

	/// <summary>
	/// Apply a single weight to every value in the exchange view with the sign determined 
	/// by wether the value is about the cutoff
	/// </summary>
	/// <param name="c"> target leverage</param>
	/// <param name="cutoff">cutoff value</param>
	void conditional_split(double c, double cutoff) {
		auto weight = c / static_cast<double>(view.size());
		for (size_t i = 0; i < view.size(); ++i) {
			// note the <= cutoff, this is to make sure that the cutoff value is included in the negative side
			if (view[i].allocation_amount <= cutoff) {
				view[i].allocation_amount = -weight;
			}
			else {
				view[i].allocation_amount = weight;
			}
		}
	}

	AGIS_API ExchangeViewAllocation& get_allocation_by_asset_index(size_t index);

	void uniform_split(double c)
	{
		auto weight = c / static_cast<double>(view.size());
		auto cutoff = view.size() / 2;
		for (size_t i = 0; i < view.size(); ++i) {
			if (i < cutoff) {
				view[i].allocation_amount = weight;
			}
			else {
				view[i].allocation_amount = -weight;
			}
		}
	}

	void linear_decreasing_weights(double _sum)
	{
		size_t N = view.size();
		double sum = static_cast<double>(N * (N + 1)) / 2; // Sum of numbers from 1 to N (cast to double)
		for (size_t i = 0; i < N; ++i) {
			view[i].allocation_amount = (_sum * (N - i) / sum);
		}
	}

	void linear_increasing_weights(double _sum)
	{
		size_t N = view.size();
		double sum = static_cast<double>(N * (N + 1)) / 2; // Sum of numbers from 1 to N (cast to double)
		for (size_t i = 0; i < N; ++i) {
			view[i].allocation_amount = (_sum * (i + 1) / sum);
		}
	}

	ExchangeView operator+(const ExchangeView& other) const {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		ExchangeView result(this->exchange, this->view.size());
		for (size_t i = 0; i < other.size(); ++i) {
			double sum = view[i].allocation_amount + other.view[i].allocation_amount;
			result.view.emplace_back(view[i].asset_index, sum);
		}
		return result;
	}
	ExchangeView operator-(const ExchangeView& other) const {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		ExchangeView result(this->exchange, this->view.size());
		for (size_t i = 0; i < other.size(); ++i) {
			double sum = view[i].allocation_amount - other.view[i].allocation_amount;
			result.view.emplace_back(view[i].asset_index, sum);
		}
		return result;
	}
	ExchangeView operator*(const ExchangeView& other) const {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		ExchangeView result(this->exchange, this->view.size());
		for (size_t i = 0; i < other.size(); ++i) {
			double sum = view[i].allocation_amount * other.view[i].allocation_amount;
			result.view.emplace_back(view[i].asset_index, sum);
		}
		return result;
	}
	ExchangeView operator/(const ExchangeView& other) const {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		ExchangeView result(this->exchange, this->view.size());
		for (size_t i = 0; i < other.size(); ++i) {
			double sum = view[i].allocation_amount / other.view[i].allocation_amount;
			result.view.emplace_back(view[i].asset_index, sum);
		}
		return result;
	}
	//============================================================================
	ExchangeView& operator+=(const ExchangeView& other) {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		for (size_t i = 0; i < other.size(); ++i) {
			view[i].allocation_amount += other.view[i].allocation_amount;
		}
		return *this;
	}
	ExchangeView& operator-=(const ExchangeView& other) {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		for (size_t i = 0; i < other.size(); ++i) {
			view[i].allocation_amount -= other.view[i].allocation_amount;
		}
		return *this;
	}
	ExchangeView& operator*=(const ExchangeView& other) {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		for (size_t i = 0; i < other.size(); ++i) {
			view[i].allocation_amount *= other.view[i].allocation_amount;
		}
		return *this;
	}
	ExchangeView& operator/=(const ExchangeView& other) {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		for (size_t i = 0; i < other.size(); ++i) {
			view[i].allocation_amount /= other.view[i].allocation_amount;
		}
		return *this;
	}
};
