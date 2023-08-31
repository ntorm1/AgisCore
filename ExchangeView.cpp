#include "pch.h"
#include "ExchangeView.h"
#include "Exchange.h"

//============================================================================
AgisResult<bool> ExchangeView::beta_scale()
{
	double original_sum = 0;
	double new_sum = 0;
	// scale each position by its beta
	for (auto& pair : this->view)
	{
		auto beta = this->exchange->get_asset_beta(pair.first);
		if (beta.is_exception()) return AgisResult<bool>(beta.get_exception());
		original_sum += pair.second;
		pair.second /= beta.unwrap();
		new_sum += pair.second;
	}
	// adjust scales to maintain new proportions but retarget to original leverage
	for(auto& pair : this->view)
	{
		pair.second *= original_sum / new_sum;
	}
	return AgisResult<bool>(true);
}


//============================================================================
AGIS_API AgisResult<bool> ExchangeView::beta_hedge(std::optional<double> target_leverage)
{
	// get the sum of the allocation.view
	double sum = 0;
	double original_sum = 0.0f;
	double beta_hedge_total = 0;
	for (auto& pair : this->view)
	{
		sum += pair.second;
		original_sum += pair.second;

		// now we need to apply the beta hedge
		auto beta = this->exchange->get_asset_beta(pair.first);
		if (beta.is_exception()) return AgisResult<bool>(beta.get_exception());
		double beta_hedge = -1 * pair.second * beta.unwrap();
		sum += abs(beta_hedge);

		beta_hedge_total += beta_hedge;
	}

	// add the new allocation
	auto market_asset = this->exchange->__get_market_asset();
	if (market_asset.is_exception()) return AgisResult<bool>(market_asset.get_exception());
	this->view.push_back({ market_asset.unwrap()->get_asset_index(),beta_hedge_total});

	// rescale by dividing by the sum and multiplying by target leverage
	for (auto& pair : this->view)
	{
		pair.second /= sum;
		pair.second *= target_leverage.value_or(original_sum);
	}

	return AgisResult<bool>(true);
}


//============================================================================
std::pair<size_t, double>& ExchangeView::get_allocation_by_asset_index(size_t index)
{
	for (auto& pair : this->view)
	{
		if (pair.first == index) return pair;
	}
	throw std::exception("Asset index not found in allocation");
}


//============================================================================
void ExchangeView::realloc(double c)
{
	for (auto& pair : this->view) {
		pair.second = c;
	}
}


//============================================================================
double ExchangeView::sum_weights(bool _abs) const
{
	// get the sum of the second element in each pair of the view
	double sum = 0;
	for (auto& pair : this->view)
	{
		if(_abs) sum += abs(pair.second);
		else sum += pair.second;
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
		auto beta = this->exchange->get_asset_beta(pair.first);
		if (beta.is_exception()) return AgisResult<double>(beta.get_exception());
		net_beta += pair.second * beta.unwrap();
	}
	return AgisResult<double>(net_beta);
}
