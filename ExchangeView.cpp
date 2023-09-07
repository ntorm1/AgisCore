#include "pch.h"
#include "ExchangeView.h"
#include "Exchange.h"
#include "Trade.h"

//============================================================================
AgisResult<bool> ExchangeView::beta_scale()
{
	double original_sum = 0;
	double new_sum = 0;
	// scale each position by its beta
	for (auto& pair : this->view)
	{
		auto beta = this->exchange->get_asset_beta(pair.asset_index);
		if (beta.is_exception()) return AgisResult<bool>(beta.get_exception());
		original_sum += pair.allocation_amount;
		pair.allocation_amount /= beta.unwrap();
		new_sum += pair.allocation_amount;
	}
	// adjust scales to maintain new proportions but retarget to original leverage
	for(auto& pair : this->view)
	{
		pair.allocation_amount *= original_sum / new_sum;
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
		if (beta.is_exception()) return AgisResult<bool>(beta.get_exception());
		
		double beta_hedge = -1 * alloc.allocation_amount * beta.unwrap();
		alloc.beta_hedge_size = beta_hedge;
		alloc.beta = beta.unwrap();
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
		if (beta.is_exception()) return AgisResult<double>(beta.get_exception());
		net_beta += pair.allocation_amount * beta.unwrap();
		net_beta += pair.beta_hedge_size.value_or(0);
	}
	return AgisResult<double>(net_beta);
}
