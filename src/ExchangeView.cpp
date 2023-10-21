#include "pch.h"
#include "Exchange.h"
#include "ExchangeMap.h"
#include "ExchangeView.h"
#include "Trade.h"
#include "AgisRisk.h"
#include "Asset/Asset.Base.h"

using namespace Agis;

//============================================================================
ExchangeView::ExchangeView(const Exchange* exchange_, size_t count, bool reserve)
{
	this->exchange = exchange_;
	if (reserve) { 
		this->view.reserve(count); this->view.reserve(count); 
	}
	else {
		this->view.resize(count);
		for (size_t i = 0; i < count; i++) {
			this->view[i].asset_index = i + exchange_->__get_exchange_offset();
		}
	}
}


//============================================================================
std::expected<bool, AgisErrorCode> ExchangeView::allocation_scale(ExchangeViewScaler t)
{
	double original_sum = 0;
	double new_sum = 0;
	// scale each position by its beta
	for (auto& pair : this->view)
	{
		std::expected<double, AgisErrorCode> scaler;
		switch (t){
		case ExchangeViewScaler::BETA: {
			scaler = this->exchange->get_asset_beta(pair.asset_index);
			break;
		}
		case ExchangeViewScaler::VOLATILITY: {
			scaler = this->exchange->get_asset_volatility(pair.asset_index);
			break;
		}
		default:
			return std::unexpected<AgisErrorCode>(AgisErrorCode::NOT_IMPLEMENTED);
		}
		if (
			!scaler.has_value()
			||
			scaler.value() == 0.0f
		) return std::unexpected<AgisErrorCode>(scaler.error());

		original_sum += pair.allocation_amount;
		pair.allocation_amount /= scaler.value();
		new_sum += pair.allocation_amount;
	}
	// adjust scales to maintain new proportions but retarget to original leverage
	for(auto& pair : this->view)
	{
		pair.allocation_amount *= original_sum / new_sum;
	}
	return true;
}


//============================================================================
AgisResult<bool> ExchangeView::vol_target_fast(double target)
{
	auto exchange_map = this->exchange->__get_exchange_map();
	auto& alloc = this->view[0];
	auto asset_opt = exchange_map->get_asset(alloc.asset_index);
	if(asset_opt.is_exception()) return AgisResult<bool>(asset_opt.get_exception());
	auto asset = asset_opt.unwrap();

	auto vol_opt = asset->get_volatility();
	if (!vol_opt.has_value()) return AgisResult<bool>(AGIS_EXCEP("invalid vol"));
	auto vol = vol_opt.value() * asset->get_unit_multiplier();

	// calculate the vol target
	double vol_target = target / vol;

	// scale exsiting allocations
	for (auto& alloc : this->view)
	{
		alloc.allocation_amount *= vol_target;
	}
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> ExchangeView::vol_target(double target)
{
	// if the view only has one asset then volatility of the portfolio is just 
	// the volatility of the asset
	if (view.size() == 0) {
		return AgisResult<bool>(true);
	}

	if (view.size() == 1) {
		return this->vol_target_fast(target);
	}

	// extract vector representation of portfolio weights
	auto exchange_map = this->exchange->__get_exchange_map();
	VectorXd weights(exchange_map->get_asset_count());
	weights.setZero();
	// set the weights by the actual allocation amounts multiplied by the specific unit
	// multiplier of the underlying asset. I.e. CL futures contract is 1000 barrels of oil
	for(auto& alloc: this->view)
	{
		auto asset = exchange_map->get_asset(alloc.asset_index);
		if (asset.is_exception()) return AgisResult<bool>(asset.get_exception());
		weights(alloc.asset_index) = alloc.allocation_amount * asset.unwrap()->get_unit_multiplier();
	}
	
	// calculate vol of existing exchange view allocation
	auto cov_matrix = exchange_map->get_covariance_matrix();
	if (cov_matrix.is_exception()) return AgisResult<bool>(cov_matrix.get_exception());
	auto vol = calculate_portfolio_volatility(weights, cov_matrix.unwrap()->get_eigen_matrix());
	if (!vol.has_value()) return AgisResult<bool>(vol.error());

	// calculate the vol target
	double vol_target = target / vol.value();

	// scale exsiting allocations
	for (auto& alloc : this->view)
	{
		alloc.allocation_amount *= vol_target;
	}
	return AgisResult<bool>(true);
}


//============================================================================
AGIS_API AgisResult<bool> ExchangeView::beta_hedge(
	std::optional<double> target_leverage
)
{
	// get the sum of the allocation.view
	double sum = 0;
	double original_sum = 0.0f;
	double beta_hedge_total = 0;
	for (auto& alloc : this->view)
	{
		sum += alloc.allocation_amount;
		original_sum += alloc.allocation_amount;

		// now we need to apply the beta hedge
		auto beta = this->exchange->get_asset_beta(alloc.asset_index);
		if (!beta.has_value()) return AgisResult<bool>(AGIS_EXCEP("invalid beta"));
		
		double beta_hedge = -1 * alloc.allocation_amount * beta.value();
		alloc.beta_hedge_size = beta_hedge;
		alloc.beta = beta.value();
		sum += abs(beta_hedge);
		beta_hedge_total += beta_hedge;
	}

	// rescale by dividing by the sum and multiplying by target leverage
	for (auto& pair : this->view)
	{
		double factor = 0;
		if (original_sum >= 1) {
			factor=target_leverage.value_or(original_sum) / sum;
		}
		else {
			factor = target_leverage.value_or(1.0) * (original_sum / sum);
		}
		pair.allocation_amount *= factor;
		pair.beta_hedge_size.value() *= factor;
	}

	// set the market asset variables
	auto market_asset  = this->exchange->__get_market_asset().unwrap();
	this->market_asset_price = market_asset->__get_market_price(true);
	this->market_asset_index = market_asset->get_asset_index();

	return AgisResult<bool>(true);
}


//============================================================================
ExchangeViewAllocation& ExchangeView::get_allocation_by_asset_index(size_t index)
{
	for (auto& pair : this->view)
	{
		if (pair.asset_index == index) return pair;
	}
	throw std::exception("Asset index not found in allocation");
}


//============================================================================
void ExchangeView::realloc(double c)
{
	for (auto& pair : this->view) {
		pair.allocation_amount = c;
	}
}


//============================================================================
double ExchangeView::sum_weights(bool _abs, bool include_beta_hedge) const
{
	// get the sum of the second element in each pair of the view
	double sum = 0;
	for (auto& pair : this->view)
	{
		if(_abs) sum += abs(pair.allocation_amount);
		if(include_beta_hedge) sum += abs(pair.beta_hedge_size.value_or(0));
		else sum += pair.allocation_amount;
	}
	return sum;
}


//============================================================================
AgisResult<double> ExchangeView::net_beta() const
{
	double net_beta = 0;
	// scale each position by its beta
	for (auto& pair : this->view)
	{
		auto beta = this->exchange->get_asset_beta(pair.asset_index);
		if (!beta.has_value()) return AgisResult<double>(AGIS_EXCEP("invalid beta"));
		net_beta += pair.allocation_amount * beta.value();
		net_beta += pair.beta_hedge_size.value_or(0);
	}
	return AgisResult<double>(net_beta);
}


//============================================================================
AGIS_API void ExchangeView::clean()
{
	// loop over the view and remove any allocations where live is false by swapping
	// elements to the end of the vector and then popping them off
	size_t i = 0;
	while (i < this->view.size())
	{
		if (!this->view[i].live)
		{
			std::swap(this->view[i], this->view.back());
			this->view.pop_back();
		}
		else
		{
			i++;
		}
	}
}
