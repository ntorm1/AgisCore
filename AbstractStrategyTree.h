#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <vector>
#include <memory>
#include "Asset.h"
#include "AgisFunctional.h"
#include "AgisErrors.h"
#include "AgisPointers.h"

class ASTNode;

class AssetNode;

class AbstractAssetLambdaNode;
class AbstractAssetLambdaRead;
class AbstractAssetLambdaChain;
class AbstractSortNode;
typedef NonNullSharedPtr<Asset> NonNullAssetPtr;

//============================================================================
class ASTNode {
public:
	virtual ~ASTNode() {}
};


//============================================================================
template <typename T>
class ExpressionNode : public ASTNode {
public:
	virtual T evaluate() const = 0;
};


//============================================================================
class StatementNode : public ASTNode {
public:
	virtual void execute() = 0;
};


//============================================================================
template <typename ExpressionReturnType>
class ValueReturningStatementNode : public ASTNode {
public:
	virtual ExpressionReturnType execute() const = 0;
};


//============================================================================
class AbstractAssetLambdaNode : public ValueReturningStatementNode<AgisResult<double>> {
public:
	virtual ~AbstractAssetLambdaNode() {}
	AbstractAssetLambdaNode() = default;
	AgisResult<double> execute() const override { return AgisResult<double>(AGIS_EXCEP("not impl")); };
	virtual AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const = 0;
};


//============================================================================
class AbstractAssetLambdaRead : public AbstractAssetLambdaNode {
public:
	AbstractAssetLambdaRead(
		AgisResult<double> (*func_)(std::shared_ptr<const Asset> const&)
	) : func(func_)
	{}
	~AbstractAssetLambdaRead() {}
	AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const override {
		return this->func(asset);
	};

private:
	AgisResult<double>(*func)(std::shared_ptr<const Asset> const&);
};


//============================================================================
class AbstractAssetLambdaOpp : public AbstractAssetLambdaNode {
public:
	AbstractAssetLambdaOpp(
		std::unique_ptr<AbstractAssetLambdaNode> left_node_,
		std::unique_ptr<AbstractAssetLambdaRead> right_read_,
		AgisOperation opperation_
	) : left_node(std::move(left_node_)),
		right_read(std::move(right_read_)),
		opperation(opperation_)
		{}
	~AbstractAssetLambdaOpp() {}

	AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const override {
		// check if right opp is null or nan
		auto res = right_read->execute(asset);
		if (res.is_exception() || res.is_nan()) return res;

		// if no left node return opp applied right node
		if(left_node == nullptr) return AgisResult<double>(
			opperation(0.0f, res.unwrap())
		);

		// if left node apply opp to left and right node
		auto left_res = left_node->execute(asset);
		if (left_res.is_exception() || left_res.is_nan()) return left_res;
		res.set_value(opperation(left_res.unwrap(), res.unwrap()));
		return res;
	}

private:
	std::unique_ptr<AbstractAssetLambdaNode> left_node = nullptr;
	NonNullUniquePtr<AbstractAssetLambdaRead> right_read;
	AgisOperation opperation;
};


//============================================================================
class AbstractAssetLambdaFilter : public AbstractAssetLambdaNode {
public:
	AbstractAssetLambdaFilter(
		NonNullUniquePtr<AbstractAssetLambdaNode> left_node_,
		AssetFilterRange const filter_range_
	) : left_node(std::move(left_node_))
	{
		this->filter = filter_range_.get_filter();
	}
	~AbstractAssetLambdaFilter() {}
	
	AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const override {
		// execute left node to get value
		auto res = left_node->execute();
		if (res.is_exception() || res.is_nan()) return res;

		// apply filter to value
		bool res_bool = filter(res.unwrap());
		if (!res_bool) res.set_value(AGIS_NAN);
		return res;
	}

private:
	NonNullUniquePtr<AbstractAssetLambdaNode> left_node;
	std::function<bool(double)> filter;
};


//============================================================================
class AbstractExchangeNode : public ExpressionNode<const Exchange*> {
public:
	AbstractExchangeNode(NonNullSharedPtr<Exchange> exchange_)
		: exchange(exchange_.get().get()) {
		if (this->exchange->get_asset_count() == 0) {
			AGIS_THROW("Exchange must have at least one asset");
		}
	}
	~AbstractExchangeNode() {}
	const Exchange* evaluate() const override {
		return this->exchange;
	}

private:
	const Exchange* exchange; // Store a const pointer to Exchange
};


//============================================================================
class AbstractExchangeViewNode : public StatementNode{

public:
	AbstractExchangeViewNode(
		std::unique_ptr<AbstractExchangeNode> exchange_node_,
		std::unique_ptr<AbstractAssetLambdaOpp> asset_lambda_op_) :
		exchange_node(std::move(exchange_node_)),
		asset_lambda_op(std::move(asset_lambda_op_))
	{
		this->exchange = exchange_node->evaluate();
		this->exchange_view = ExchangeView(
			this->exchange, 
			this->exchange->get_asset_count(),
			false
		);
	}
	~AbstractExchangeViewNode() {}

	ExchangeView get_view() {
		return this->exchange_view;
	}

	size_t size() {
		return this->exchange_view.view.size();
	}

	void execute() override {
		auto& view = exchange_view.view;

		for (auto& asset : this->exchange->get_assets())
		{
			if (!asset || !asset->__in_exchange_view) continue;
			if (!asset->__is_streaming) continue;
			auto val = this->asset_lambda_op->execute(asset);
			if (val.is_exception()) {

			}
			auto v = val.unwrap();
			view[asset->get_asset_index()].allocation_amount = v;
		}
	}

private:
	ExchangeView exchange_view;
	const Exchange* exchange;
	NonNullUniquePtr<AbstractExchangeNode> exchange_node;
	NonNullUniquePtr<AbstractAssetLambdaOpp> asset_lambda_op;
};


//============================================================================
class AbstractSortNode : ValueReturningStatementNode<ExchangeView> {
public:
	AbstractSortNode(
		std::unique_ptr<AbstractExchangeViewNode> ev_,
		int N_,
		ExchangeQueryType query_type_
	) : 
		ev(std::move(ev_))	{
		this->N = (N_ == -1) ? ev->size() : static_cast<size_t>(N);
		this->query_type = query_type_;
	}

	ExchangeView execute() const override {
		this->ev->execute();
		auto view = this->ev->get_view();
		view.sort(N, query_type);
		return view;
	}

private:
	NonNullUniquePtr<AbstractExchangeViewNode> ev;
	size_t N;
	ExchangeQueryType query_type;
};


//============================================================================
class AbstractGenAllocationNode : public StatementNode {
public:
	AbstractGenAllocationNode(
		ExchangeView(*func_)(),
		ExchangeViewOpp ev_opp_type_,
		double target,
		std::optional<double> ev_opp_param
	) :
		func(func_),
		ev_opp_type(ev_opp_type_),
		target(target),
		ev_opp_param(ev_opp_param)
	{}

	void execute() override {
		auto view = func();
		switch (this->ev_opp_type)
		{
			case ExchangeViewOpp::UNIFORM: {
				view.uniform_weights(target);
				break;
			}
			case ExchangeViewOpp::LINEAR_INCREASE: {
				view.linear_increasing_weights(target);
				break;
			}
			case ExchangeViewOpp::LINEAR_DECREASE: {
				view.linear_decreasing_weights(target);
				break;
			}
			case ExchangeViewOpp::CONDITIONAL_SPLIT: {
				if (!this->ev_opp_param.has_value()) throw std::runtime_error("conditional split requires parameter");
				view.conditional_split(target, this->ev_opp_param.value());
				break;
			}
			case ExchangeViewOpp::UNIFORM_SPLIT: {
				view.uniform_split(target);
				break;
			}
			case ExchangeViewOpp::CONSTANT: {
				auto c = this->ev_opp_param.value() * target;
				//ev.constant_weights(c, this->trades);
				break;
			}
			default: {
				throw std::runtime_error("invalid exchange view operation");
			}
		}
	}

private:
	ExchangeView(*func)();
	ExchangeViewOpp ev_opp_type;
	double target;
	std::optional<double> ev_opp_param;
};


