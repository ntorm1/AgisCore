#pragma once
#include <cmath> // For std::sqrt
#include <vector>

// Calculate covariance between two sets of values over a range [start, end)
double covariance(const std::vector<double>& values1, const std::vector<double>& values2, size_t start, size_t end);

// Calculate variance of a set of values over a range [start, end)
double variance(const std::vector<double>& values, size_t start, size_t end);

// Calculate the mean of a set of values over a range [start, end)
double mean(const std::vector<double>& values, size_t start, size_t end);

// Calculate the correlation coefficient between two sets of values over a range [start, end)
double correlation(const std::vector<double>& values1, const std::vector<double>& values2, size_t start, size_t end);

std::vector<double> rolling_beta(
	const std::vector<double>& stockReturns,
	const std::vector<double>& marketReturns,
	size_t windowSize
);
