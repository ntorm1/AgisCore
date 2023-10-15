#include "pch.h"
#include <type_traits>

#include "AgisStrategyTracers.h"
#include "AgisStrategy.h"
#include "AgisRisk.h"


//============================================================================
AgisStrategyTracers::AgisStrategyTracers(AgisStrategy* strategy_)
{
	// note: cash and nlv set in strategy constructor
	this->strategy = strategy_;
	this->set(Tracer::CASH);
	this->set(Tracer::NLV);
};


//============================================================================
AgisStrategyTracers::AgisStrategyTracers(Portfolio* portfolio, double cash)
{
	this->strategy = nullptr;
	this->starting_cash.store(cash);
	this->cash.store(cash);
	this->nlv.store(cash);
	this->set(Tracer::CASH);
	this->set(Tracer::NLV);
};


//============================================================================
std::optional<double> AgisStrategyTracers::get(Tracer tracer) const
{
	switch (tracer)
	{
	case Tracer::BETA:
		if (this->has(Tracer::BETA)) return this->net_beta.load();
		return std::nullopt;
	case Tracer::VOLATILITY:
		if (this->has(Tracer::VOLATILITY)) return this->portfolio_volatility.load();
		return std::nullopt;
	case Tracer::LEVERAGE:
		if (this->has(Tracer::LEVERAGE)) return this->net_leverage_ratio.load();
		return std::nullopt;
	case Tracer::CASH:
		return this->cash.load();
		break;
	case Tracer::NLV:
		return this->nlv.load();
		break;
	}
	return std::nullopt;
}


//============================================================================
void AgisStrategyTracers::reset_history()
{
	this->cash_history.clear();
	this->nlv_history.clear();
	this->beta_history.clear();
	this->net_leverage_ratio_history.clear();
	this->portfolio_volatility_history.clear();

	this->cash.store(this->starting_cash);
	this->nlv.store(this->cash.load());
	this->net_beta = 0.0f;
	this->net_leverage_ratio = 0.0f;
}


//============================================================================
std::expected<double, AgisException>
AgisStrategyTracers::get_portfolio_volatility()
{
	// check if benchmark strategy, if so use the benchmark asset's variance to calculate volatility
	if (this->strategy->get_strategy_type() == AgisStrategyType::BENCHMARK) {
		return this->get_benchmark_volatility();
	}
	auto cov_matrix = this->strategy->get_covariance_matrix();
	if (cov_matrix.is_exception()) {
		return std::unexpected<AgisException>(AGIS_FORWARD_EXCEP(cov_matrix.get_exception()));
	}

	// set the portfolio weights to the trades, note the vector already has the nlv of the trades.
	// so all we have to do is divide by the nlv to get the pct portfolio weights
	for (auto& [asset_index, trade] : this->strategy->trades)
	{
		this->portfolio_weights(asset_index) /= this->nlv;
	}

	// calculate vol using the covariance matrix
	auto res = calculate_portfolio_volatility(this->portfolio_weights, cov_matrix.unwrap()->get_eigen_matrix());
	if (!res.has_value()) return res;
	this->portfolio_volatility = res.value();
	return res;

}


//============================================================================
std::expected<double, AgisException>
AgisStrategyTracers::get_benchmark_volatility()
{
	// override the portfolio volatility calculation to use the benchmark asset's variance 
	// to shortcut the valculation of portfolio volatility. This relies on the fact the benchmark
	// strategy invests all funds into the benchmark asset at t0 and does nothing after.
	auto cov_matrix = this->strategy->get_covariance_matrix();
	if (cov_matrix.is_exception()) {
		return std::unexpected<AgisException>(AGIS_FORWARD_EXCEP(cov_matrix.get_exception()));
	}

	auto bench_strategy = dynamic_cast<BenchMarkStrategy*>(this->strategy);
	auto& eigen_matrix = cov_matrix.unwrap()->get_eigen_matrix();
	auto variance = eigen_matrix(bench_strategy->asset_index, bench_strategy->asset_index);
	
	this->portfolio_volatility = std::sqrt(variance * 252);
	return this->portfolio_volatility;
}

   
//============================================================================
AgisResult<bool> AgisStrategyTracers::evaluate()
{
	// Note: at this point all trades have been evaluated and the cash balance has been updated
	// so we only have to observer the values or use them to calculate other values.
	if (this->has(Tracer::NLV)) this->nlv_history.push_back(
		this->nlv.load()
	);
	if (this->has(Tracer::CASH)) this->cash_history.push_back(this->cash.load());

	if (this->has(Tracer::BETA)) {
		this->beta_history.push_back(this->net_beta.load());
	}

	if (this->has(Tracer::LEVERAGE)) {
		// right now net_leverage_ratio has the sum of athe absolute values of the positions
		// to get the leverage ratio we need to divide the nlv
		this->net_leverage_ratio_history.push_back(this->net_leverage_ratio.load() / this->nlv.load());
	}

	if (this->has(Tracer::VOLATILITY)) {
		auto v = this->get_portfolio_volatility();
		if (!v.has_value()) {
			return AgisResult<bool>(AGIS_FORWARD_EXCEP(v.error()));
		}
		this->portfolio_volatility_history.push_back(v.value());
	}
	return AgisResult<bool>(true);
}


//============================================================================
void AgisStrategyTracers::zero_out_tracers()
{
	this->nlv.store(this->cash.load());
	if (this->has(Tracer::BETA)) this->net_beta = 0.0f;
	if (this->has(Tracer::LEVERAGE)) this->net_leverage_ratio = 0.0f;
	if (this->strategy->limits.max_leverage.has_value()) this->strategy->limits.phantom_cash = 0.0f;
}


//============================================================================
void AgisStrategyTracers::set_portfolio_weight(size_t index, double v)
{
	this->portfolio_weights(index) = v;
}
