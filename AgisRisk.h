#pragma once
#include <cmath> // For std::sqrt
#include <vector>
#include <span>

class Asset;

struct IncrementalCovariance
{
	IncrementalCovariance(
		std::shared_ptr<Asset> a1,
		std::shared_ptr<Asset> a2,
		size_t period
	);

	IncrementalCovariance(
		std::span<double> p1_,
		std::span<double>p2_,
		size_t period_,
		size_t current_index_
	) {
#ifdef _DEBUG
		if (p1_.size() < period_ || p2_.size() < period_) {
			throw std::runtime_error("Period is larger than the size of the input arrays.");
		}
		if (p1_.size() != p2_.size()) {
			throw std::runtime_error("Input arrays must be the same size.");
		}
		if (current_index_ >= period_) {
			throw std::runtime_error("Current index must be less than the period.");
		}
#endif
		this->p1 = p1_;
		this->p2 = p2_;
		this->period = period_;
		size_t upper_bound = 0;
		if (current_index_ < period - 1) {
			this->i = 0;
			upper_bound = current_index_ + 1;
		}
		else {
			this->i = current_index_ - (period - 1);
			upper_bound = this->i + period;
		}
		for (this->i; this->i < upper_bound; ++this->i) {
			sum1 += p1[i];
			sum2 += p2[i];
			sum_product += p1[i] * p2[i];
			sum1_squared += p1[i] * p1[i];
			sum2_squared += p2[i] * p2[i];
		}
		if (this->i <= period - 1) {
			this->covariance = 0;
		}
		else {
			this->covariance = (sum_product - sum1 * sum2 / period) / (period - 1);
		}
	}

	void step()
	{
		if (i > (period-1)) {
			auto prev_index = (i - 1) - (period - 1);
			sum1 = sum1 - p1[prev_index] + p1[i];
			sum2 = sum2 - p2[prev_index] + p2[i];
			sum_product = sum_product - p1[prev_index] * p2[prev_index] + p1[i] * p2[i];
			sum1_squared = sum1_squared - p1[prev_index] * p1[prev_index] + p1[i] * p1[i];
			sum2_squared = sum2_squared - p2[prev_index] * p2[prev_index] + p2[i] * p2[i];
			this->covariance = (sum_product - sum1 * sum2 / period) / (period - 1);
		}
		else {
			sum1 += p1[i];
			sum2 += p2[i];
			sum_product += p1[i] * p2[i];
			sum1_squared += p1[i] * p1[i];
			sum2_squared += p2[i] * p2[i];
			if (i == (period - 1)) {
				this->covariance = (sum_product - sum1 * sum2 / period) / (period - 1);
			}
		}
		i++;
	}

	double get_covariance()
	{
		return covariance;
	}

private:
	std::span<double>p1;
	std::span<double>p2;
	size_t i;
	size_t period;
	double sum1 = 0;
	double sum2 = 0;
	double sum_product = 0;
	double sum1_squared = 0;
	double sum2_squared = 0;
	double covariance;
};

// Calculate covariance between two sets of values over a range [start, end)
double covariance(const std::vector<double>& values1, const std::vector<double>& values2, size_t start, size_t end);

// Calculate variance of a set of values over a range [start, end)
double variance(const std::vector<double>& values, size_t start, size_t end);

// Calculate the mean of a set of values over a range [start, end)
double mean(const std::vector<double>& values, size_t start, size_t end);
double mean(const double* values, size_t start, size_t end);


// Calculate the correlation coefficient between two sets of values over a range [start, end)
double correlation(const std::vector<double>& values1, const std::vector<double>& values2, size_t start, size_t end);

std::vector<double> rolling_beta(
	const std::vector<double>& stockReturns,
	const std::vector<double>& marketReturns,
	size_t windowSize
);
