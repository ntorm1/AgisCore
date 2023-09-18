#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <vector>
#include <span>
#include <optional>
#include <Eigen/Dense>
#include "Asset.h"

using namespace Eigen;

typedef Matrix<double, Dynamic, Dynamic> MatrixXd;
typedef Matrix<double, Dynamic, 1> VectorXi;

class Order;
class AgisStrategy;
class ExchangeMap;
struct AgisCovarianceMatrix;
class IncrementalCovariance;
class AssetObserver;


/**
 * @brief Container for holding the covariance matrix for all assets listed on an exchange. It contains 
 * a matrix of covariances between all assets, as well as a vector of vectors of incremental covariance structs
 * used to update the cov matrix on exchange step forward.
*/
struct AgisCovarianceMatrix
{
	AgisCovarianceMatrix(ExchangeMap* exchange_map, size_t lookback = 252, size_t step_size = 1);
	
	// Overload for accessing the covariance matrix
	double operator()(size_t i, size_t j) const noexcept {
		return covariance_matrix(i,j);
	}

	/**
	 * @brief enable the covariance matrix tracing by adding observers to all assets
	*/
	void set_asset_observers() noexcept;

	/**
	 * @brief disable the covariance matrix tracing by removing all observers from the assets
	*/
	void clear_observers() noexcept;

	/**
	 * @brief get the underlying eigen matrix of covariance values
	 * @return 
	*/
	auto const & get_eigen_matrix() const noexcept {return this->covariance_matrix; }

	size_t get_step_size() const noexcept { return this->step_size; }
	size_t get_lookback() const noexcept { return this->lookback; }

private:
	/**
	 * @brief container for incremental covariance structs used to update the
	 * covariance matrix on exchange step forward. Not only lower diagonal is stored, use symmetry to fill rest
	*/
	std::vector<std::shared_ptr<IncrementalCovariance>> incremental_covariance_matrix;

	/**
	 * @brief Main covariance matrix containing the covariance between all assets in the exchange map
	*/
	MatrixXd covariance_matrix;

	ExchangeMap* exchange_map = nullptr;

	bool built = false;

	size_t lookback = 0;
	size_t step_size = 1;

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

/**
 * @brief calculate the rolling beta of a stock over a given window size
 * @param stock_returns returns of the asset
 * @param market_returns returns of the benchmark
 * @param windowSize the size of the window to calculate the beta over
 * @return the rolling beta
*/
std::vector<double> rolling_beta(
	std::vector<double> const& stock_returns,
	std::vector<double> const& market_returns,
	size_t windowSize
);

/**
 * @brief calculate the rolling volatility of a stock over a given window size
 * @param returns returns of the asset
 * @param windowSize the size of the window to calculate the volatility over
 * @return the rolling volatility
*/
AGIS_API std::vector<double> rolling_volatility(
	std::span<double> const returns,
	size_t windowSize
);


AGIS_API AgisResult<double> calculate_portfolio_volatility(
	VectorXd const& portfolio_weights,
	MatrixXd const& covariance_matrix
);


struct AgisRiskStruct
{
	AgisRiskStruct() {};

	void __build(AgisStrategy const* parent_strategy);

	void __reset();

	double estimate_phantom_cash(Order const* order);

	/**
	 * @brief const pointer to the parent strategy
	*/
	AgisStrategy const * parent_strategy = nullptr;

	/**
	 * @brief const pointer to the exchange map
	*/
	ExchangeMap const * exchange_map = nullptr;

	/**
	* @brief The max portfolio leverage allowed for the strategy
	*/
	std::optional<double> max_leverage = std::nullopt;

	/**
	 * @brief phantom cash traces the amount of cash required to execure the orders during the current
	 * call to the strategies update function. This is used to calculate the max leverage in the 
	 * middle of the update function to prevent the strategy from going over the max leverage.
	*/
	double phantom_cash = 0.0f;

	/**
	 * @brief wether or not to allow shorting
	*/
	bool allow_shorting = true;

	/**
	 * @brief a vector of size equal to the number of assets available to the strategy that contains
	 * the number of units held by the asset of index i in index i of the vector.
	*/
	std::vector<double> asset_holdings;
};
