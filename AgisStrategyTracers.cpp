#include "pch.h"
#include "AgisStrategyTracers.h"
#include "AgisStrategy.h"

//============================================================================
std::optional<double> AgisStrategyTracers::get(Tracer tracer) const
{
	switch (tracer)
	{
	case Tracer::BETA:
		if (this->has(Tracer::BETA)) return this->net_beta;
		return std::nullopt;
	case Tracer::VOLATILITY:
		if (this->has(Tracer::VOLATILITY)) return this->portfolio_volatility;
		return std::nullopt;
	case Tracer::LEVERAGE:
		if (this->has(Tracer::LEVERAGE)) return this->net_leverage_ratio;
		return std::nullopt;
	case Tracer::CASH:
		return this->cash;
		break;
	case Tracer::NLV:
		return this->nlv;
		break;
	}
}


//============================================================================
void AgisStrategyTracers::build(AgisStrategy* strategy, size_t n)
{
	if (this->has(Tracer::BETA)) this->beta_history.reserve(n);

	if (this->has(Tracer::LEVERAGE)) this->net_leverage_ratio_history.reserve(n);

	if (this->has(Tracer::VOLATILITY)) {
		this->portfolio_volatility_history.reserve(n);

		// init eigen vector of portfolio weights
		auto asset_count = strategy->exchange_map->get_asset_count();
		this->portfolio_weights.resize(asset_count);
		this->portfolio_weights.setZero();
	}

	this->cash = strategy->portfolio_allocation * strategy->portfolio->get_cash();
	this->nlv = this->cash;

	this->nlv_history.reserve(n);
	this->cash_history.reserve(n);
}


//============================================================================
void AgisStrategyTracers::reset_history()
{
	this->cash_history.clear();
	this->nlv_history.clear();
	this->beta_history.clear();
	this->net_leverage_ratio_history.clear();
	this->portfolio_volatility_history.clear();

	this->cash = this->starting_cash;
	this->nlv = this->cash;
	this->net_beta = 0.0f;
	this->net_leverage_ratio = 0.0f;
}


//============================================================================
AgisResult<bool> AgisStrategyTracers::evaluate()
{
	// Note: at this point all trades have been evaluated and the cash balance has been updated
	// so we only have to observer the values or use them to calculate other values.
	this->nlv_history.push_back(this->nlv);
	this->cash_history.push_back(this->cash);

	if (this->has(Tracer::BETA)) {
		this->beta_history.push_back(this->net_beta);
	}

	if (this->has(Tracer::LEVERAGE)) {
		// right now net_leverage_ratio has the sum of athe absolute values of the positions
		// to get the leverage ratio we need to divide the nlv
		this->net_leverage_ratio_history.push_back(this->net_leverage_ratio / this->nlv);
	}

	if (this->has(Tracer::VOLATILITY)) {
		auto v = this->strategy->calculate_portfolio_volatility();
		if (v.is_exception()) {
			return AgisResult<bool>(AGIS_FORWARD_EXCEP(v.get_exception()));
		}
		this->portfolio_volatility_history.push_back(v.unwrap());
	}
	return AgisResult<bool>(true);
}


//============================================================================
void AgisStrategyTracers::zero_out_tracers()
{
	this->nlv = this->cash;
	if (this->has(Tracer::BETA)) this->net_beta = 0.0f;
	if (this->has(Tracer::LEVERAGE)) this->net_leverage_ratio = 0.0f;
	if (this->strategy->limits.max_leverage.has_value()) this->strategy->limits.phantom_cash = 0.0f;
}


//============================================================================
void AgisStrategyTracers::set_portfolio_weight(size_t index, double v)
{
	this->portfolio_weights(index) = v;
}
