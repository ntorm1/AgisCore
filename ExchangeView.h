#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h" 
#include "AgisErrors.h"

class Exchange;
class ExchangeMap;


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
	UNIFORM,
	LINEAR_DECREASE,
	LINEAR_INCREASE,
	CONDITIONAL_SPLIT,
	UNIFORM_SPLIT
};


AGIS_API extern std::vector<std::string> exchange_view_opps;

AGIS_API std::string ev_opp_to_str(ExchangeViewOpp ev_opp);
AGIS_API std::string ev_query_type(ExchangeQueryType ev_query);

struct ExchangeView
{
	std::vector<std::pair<size_t, double>> view;
	Exchange* exchange;

	ExchangeView() = default;
	ExchangeView(Exchange* exchange_, size_t count) {
		this->exchange = exchange_;
		this->view.reserve(count);
	}

	/// <summary>
	/// Return the number of elements in the exchange view
	/// </summary>
	/// <returns></returns>
	size_t size() const { return this->view.size(); }

	/// <summary>
	/// Take an exchange view, then sort and extract a subset of the view
	/// </summary>
	/// <param name="N">number of elements to retunr</param>
	/// <param name="sort_type">type of sort to do</param>
	void sort(size_t N, ExchangeQueryType sort_type);

	/// <summary>
	/// Sum of all weights in the exchange view
	/// </summary>
	/// <returns></returns>
	AGIS_API double sum_weights(bool _abs = false) const;

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


	/// <summary>
	/// Generate a beta hedge for the portfolio and adjust allocation weights to match 
	/// the target leverage of the portfolio. Note assumes the allocations are % of nlv
	/// </summary>
	/// <param name="target_leverage">the target leverage of the strategy</param>
	/// <returns></returns>
	AGIS_API AgisResult<bool> beta_hedge(double target_leverage);

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
			return lhs.second < rhs.second;
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
			pair.second = weight;
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
			if (view[i].second <= cutoff) {
				view[i].second = -weight;
			}
			else {
				view[i].second = weight;
			}
		}
	}

	AGIS_API std::pair<size_t, double>& get_allocation_by_asset_index(size_t index);

	void uniform_split(double c)
	{
		auto weight = c / static_cast<double>(view.size());
		auto cutoff = view.size() / 2;
		for (size_t i = 0; i < view.size(); ++i) {
			if (i < cutoff) {
				view[i].second = weight;
			}
			else {
				view[i].second = -weight;
			}
		}
	}

	void linear_decreasing_weights(double _sum)
	{
		size_t N = view.size();
		double sum = static_cast<double>(N * (N + 1)) / 2; // Sum of numbers from 1 to N (cast to double)
		for (size_t i = 0; i < N; ++i) {
			view[i].second = (_sum * (N - i) / sum);
		}
	}

	void linear_increasing_weights(double _sum)
	{
		size_t N = view.size();
		double sum = static_cast<double>(N * (N + 1)) / 2; // Sum of numbers from 1 to N (cast to double)
		for (size_t i = 0; i < N; ++i) {
			view[i].second = (_sum * (i + 1) / sum);
		}
	}

	ExchangeView operator+(const ExchangeView& other) const {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		ExchangeView result(this->exchange, this->view.size());
		for (size_t i = 0; i < other.size(); ++i) {
			double sum = view[i].second + other.view[i].second;
			result.view.emplace_back(view[i].first, sum);
		}
		return result;
	}
	ExchangeView operator-(const ExchangeView& other) const {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		ExchangeView result(this->exchange, this->view.size());
		for (size_t i = 0; i < other.size(); ++i) {
			double sum = view[i].second - other.view[i].second;
			result.view.emplace_back(view[i].first, sum);
		}
		return result;
	}
	ExchangeView operator*(const ExchangeView& other) const {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		ExchangeView result(this->exchange, this->view.size());
		for (size_t i = 0; i < other.size(); ++i) {
			double sum = view[i].second * other.view[i].second;
			result.view.emplace_back(view[i].first, sum);
		}
		return result;
	}
	ExchangeView operator/(const ExchangeView& other) const {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		ExchangeView result(this->exchange, this->view.size());
		for (size_t i = 0; i < other.size(); ++i) {
			double sum = view[i].second / other.view[i].second;
			result.view.emplace_back(view[i].first, sum);
		}
		return result;
	}
	//============================================================================
	ExchangeView& operator+=(const ExchangeView& other) {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		for (size_t i = 0; i < other.size(); ++i) {
			view[i].second += other.view[i].second;
		}
		return *this;
	}
	ExchangeView& operator-=(const ExchangeView& other) {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		for (size_t i = 0; i < other.size(); ++i) {
			view[i].second -= other.view[i].second;
		}
		return *this;
	}
	ExchangeView& operator*=(const ExchangeView& other) {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		for (size_t i = 0; i < other.size(); ++i) {
			view[i].second *= other.view[i].second;
		}
		return *this;
	}
	ExchangeView& operator/=(const ExchangeView& other) {
		CHECK_INDEX_MATCH(*this, other);
		CHECK_SIZE_MATCH(*this, other);
		for (size_t i = 0; i < other.size(); ++i) {
			view[i].second /= other.view[i].second;
		}
		return *this;
	}
};
