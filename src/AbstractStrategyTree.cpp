#include "pch.h"
#include "AbstractStrategyTree.h"

import Asset;

using namespace Agis;



AbstractAssetLambdaLogical::AbstractAssetLambdaLogical(
	std::unique_ptr<AbstractAssetLambdaNode> left_node_,
	AgisLogicalType logical_type_,
	AgisLogicalRightVal right_node_,
	bool numeric_cast
) :
	left_node(std::move(left_node_)),
	logical_type(logical_type_),
	right_node(std::move(right_node_)),
	AbstractAssetLambdaNode(AssetLambdaType::LOGICAL)
{
	// set the warmup equal to the left node warmup and optionally the max of that and the right node
	this->warmup = this->left_node->get_warmup();
	if (std::holds_alternative<std::unique_ptr<AbstractAssetLambdaNode>>(this->right_node)) {
		auto& right_val_node = std::get<std::unique_ptr<AbstractAssetLambdaNode>>(this->right_node);
		this->warmup = std::max(this->warmup, right_val_node->get_warmup());
	}
}


//============================================================================
AgisResult<double>
AbstractAssetLambdaLogical::execute(std::shared_ptr<const Asset> const& asset) const {
	// execute left node to get value
	AgisResult<double> res = left_node->execute();
	bool res_bool = false;
	if (res.is_exception() || res.is_nan()) return res;

	// pass the result of the left node to the logical opperation with the right node
	// that is either a scaler double or another asset lambda node
	if (std::holds_alternative<double>(this->right_node)) {
		auto right_val_double = std::get<double>(this->right_node);
		res_bool = this->logical_compare(res.unwrap(), right_val_double);
		if (!res_bool && !this->numeric_cast) res.set_value(AGIS_NAN);
	}
	else {
		auto& right_val_node = std::get<std::unique_ptr<AbstractAssetLambdaNode>>(this->right_node);
		auto right_res = right_val_node->execute();
		if (right_res.is_exception() || right_res.is_nan()) return right_res;
		res_bool = this->logical_compare(res.unwrap(), right_res.unwrap());
		if (!res_bool && !this->numeric_cast) res.set_value(AGIS_NAN);
	}

	// if numeric cast, take the boolean result of the logical opperation and cast to double
	// i.e. if the logical opperation is true, return 1.0, else return 0.0
	if (this->numeric_cast)	res.set_value(static_cast<double>(res_bool));
	return res;
}


//============================================================================
AgisResult<double>
AbstractAssetObserve::execute(std::shared_ptr<const Asset> const& asset) const {
	return asset->get_asset_observer_result(this->observer_name);
};


//============================================================================
void
AbstractAssetLambdaRead::set_col_index_lambda(size_t col_index) {
	auto l = [=](std::shared_ptr<const Asset> const& asset) -> AgisResult<double> {
		return asset->get_asset_feature(col_index, index.value());
		};
	this->func = l;
}

//============================================================================
AGIS_API std::unique_ptr<AbstractAssetLambdaRead> create_asset_lambda_read(std::string col, int index) {
	return std::make_unique<AbstractAssetLambdaRead>(col, index);
}


//============================================================================
AgisResult<bool> AbstractAssetObserve::set_warmup(const Exchange* exchange)
{
	// set the warmup by finding the observer in the first asset in the exchange
	auto& assets = exchange->get_assets();
	assert(assets.size());
	auto observer = assets[0]->get_observer(this->observer_name);
	if (observer.is_exception()) return AgisResult<bool>(observer.get_exception());
	this->warmup = observer.unwrap()->get_warmup();
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> AbstractExchangeViewNode::execute() {
	auto& view = exchange_view.view;

	size_t i = 0;
	for (auto& asset : this->assets)
	{
		if ((!asset || !asset->__in_exchange_view) ||
			(!asset->__is_streaming) ||
			(asset->get_current_index() < this->warmup)) {
			view[i].live = false;
			i++;
			continue;
		}
		auto val = this->asset_lambda_op->execute(asset);
		// forward any exceptions
		if (val.is_exception()) {
			return AgisResult<bool>(val.get_exception());
		}
		// disable asset if nan
		if (val.is_nan()) {
			view[i].live = false;
			continue;
		}
		auto v = val.unwrap();
		view[i].allocation_amount = v;
		view[i].live = true;
		i++;
	}
	return AgisResult<bool>(true);
}


//============================================================================
void AbstractExchangeViewNode::apply_asset_index_filter(std::vector<size_t> const& index_keep)
{
	// remove asset pointers in the node's asset list if index not in index_keep
	for (auto asset_iter = this->assets.begin(); asset_iter != this->assets.end();)
	{
		auto& asset = *asset_iter;
		size_t asset_index = asset->get_asset_index();
		auto it = std::find(index_keep.begin(), index_keep.end(), asset_index);
		if (it != index_keep.end()) {
			++asset_iter;
			continue;
		};
		asset_iter = this->assets.erase(asset_iter);
	}
	// pop elements from the view if index not in index_keep
	auto& view = this->exchange_view.view;
	for (auto& allocation : view)
	{
		auto it = std::find(index_keep.begin(), index_keep.end(), allocation.asset_index);
		if (it != index_keep.end()) continue;
		// swap to end and pop
		std::swap(allocation, view.back());
		view.pop_back();
	}
}


//============================================================================
std::unique_ptr<AbstractAssetLambdaOpp> create_asset_lambda_opp(
	std::unique_ptr<AbstractAssetLambdaNode>& left_node,
	std::unique_ptr<AbstractAssetLambdaRead>& right_read,
	AgisOpperationType opperation
) {
	AgisOperation opp;
	switch (opperation)
	{
	case AgisOpperationType::INIT:
		opp = agis_init;
		break;	
	case AgisOpperationType::IDENTITY:
		opp = agis_identity;
		break;
	case AgisOpperationType::ADD:
		opp = agis_add;
		break;
	case AgisOpperationType::SUBTRACT:
		opp = agis_subtract;
		break;
	case AgisOpperationType::MULTIPLY:
		opp = agis_multiply;
		break;
	case AgisOpperationType::DIVIDE:
		opp = agis_divide;
		break;
	default:
		break;
	}
	return std::make_unique<AbstractAssetLambdaOpp>(
		std::move(left_node),
		std::move(right_read),
		opp
	);
}


//============================================================================
std::unique_ptr<AbstractExchangeNode> create_exchange_node(
	ExchangePtr const exchange) {
	return std::make_unique<AbstractExchangeNode>(exchange);
}


//============================================================================
std::unique_ptr<AbstractExchangeViewNode> create_exchange_view_node(
	std::unique_ptr<AbstractExchangeNode>& exchange_node,
	std::unique_ptr<AbstractAssetLambdaOpp>& asset_lambda_op
) {
	return std::make_unique<AbstractExchangeViewNode>(
		std::move(exchange_node),
		std::move(asset_lambda_op)
	);
}


//============================================================================
AGIS_API std::unique_ptr<AbstractSortNode> create_sort_node(
	std::unique_ptr<AbstractExchangeViewNode>& ev,
	int N,
	ExchangeQueryType query_type
) {
	return std::make_unique<AbstractSortNode>(
		std::move(ev),
		N,
		query_type
	);
}


//============================================================================
AGIS_API std::unique_ptr<AbstractGenAllocationNode> create_gen_alloc_node(
	std::unique_ptr<AbstractSortNode>& sort_node,
	ExchangeViewOpp ev_opp_type,
	double target,
	std::optional<double> ev_opp_param
) {
	return std::make_unique<AbstractGenAllocationNode>(
		std::move(sort_node),
		ev_opp_type,
		target,
		ev_opp_param
	);
}


//============================================================================
AGIS_API std::unique_ptr<AbstractStrategyAllocationNode> create_strategy_alloc_node(
	AgisStrategy* strategy_,
	std::unique_ptr<AbstractGenAllocationNode>& gen_alloc_node_,
	double epsilon_,
	bool clear_missing_,
	std::optional<TradeExitPtr> exit_,
	AllocType alloc_type_)
{
	return std::make_unique<AbstractStrategyAllocationNode>(
		strategy_,
		std::move(gen_alloc_node_),
		epsilon_,
		clear_missing_,
		exit_,
		alloc_type_
	);
}