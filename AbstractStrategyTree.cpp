#include "pch.h"
#include "AbstractStrategyTree.h"
#include "AgisObservers.h"

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
AGIS_API std::unique_ptr<AbstractAssetLambdaOpp> create_asset_lambda_opp(
	std::unique_ptr<AbstractAssetLambdaNode>& left_node,
	std::unique_ptr<AbstractAssetLambdaRead>& right_read,
	std::string const& opperation
) {
	if (!agis_function_map.contains(opperation)) throw std::runtime_error("invalid opperation");
	AgisOperation enum_opperation = agis_function_map.at(opperation);
	return std::make_unique<AbstractAssetLambdaOpp>(
		std::move(left_node),
		std::move(right_read),
		enum_opperation
	);
}


//============================================================================
AGIS_API std::unique_ptr<AbstractExchangeNode> create_exchange_node(
	ExchangePtr const exchange) {
	return std::make_unique<AbstractExchangeNode>(exchange);
}


//============================================================================
AGIS_API std::unique_ptr<AbstractExchangeViewNode> create_exchange_view_node(
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