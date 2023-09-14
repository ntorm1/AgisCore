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

template <typename T>
class ASTNode;

class AssetNode;

class AssetLambdaNode;
class AssetLambdaRead;
class AssetLambdaChain;

typedef NonNullSharedPtr<Asset> NonNullAssetPtr;

//============================================================================
template <typename T>
class ASTNode {
public:
	virtual ~ASTNode() {}
};


//============================================================================
template <typename T>
class ExpressionNode : public ASTNode<T> {
public:
	virtual T evaluate() const = 0;
};


//============================================================================
class StatementNode : public ASTNode<void> {
public:
	virtual void execute() = 0;
};


//============================================================================
template <typename ExpressionReturnType>
class ValueReturningStatementNode : public ASTNode<ExpressionReturnType> {
public:
	virtual ExpressionReturnType execute() const = 0;
};


//============================================================================
class AssetLambdaNode : public ValueReturningStatementNode<AgisResult<double>> {
public:
	virtual ~AssetLambdaNode() {}
	AssetLambdaNode() = default;
	AgisResult<double> execute() const override {};
	virtual AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const = 0;
};


//============================================================================
class AssetLambdaRead : public AssetLambdaNode {
public:
	AssetLambdaRead(
		AgisResult<double> (*func)(std::shared_ptr<const Asset> const&)
	)
	{}
	~AssetLambdaRead() {}
	AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const override {
		return this->func(asset);
	};

private:
	AgisResult<double>(*func)(std::shared_ptr<const Asset> const&);
};


//============================================================================
class AssetLambdaOpp : public AssetLambdaNode {
public:
	AssetLambdaOpp(
		std::shared_ptr<AssetLambdaNode> left_node_,
		NonNullSharedPtr<AssetLambdaRead> right_read_,
		AgisOperation opperation_
	) : left_node(left_node_),
		right_read(right_read_),
		opperation(opperation_)
		{}
	~AssetLambdaOpp() {}

	AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const override {
		// check if right opp is null or nan
		auto res = right_read->execute(asset);
		if (res.is_exception() || res.is_nan()) return res;

		// if no left node return opp applied right node
		if(left_node == nullptr) return AgisResult<double>(
			opperation(0.0f, res.unwrap())
		);

		// if left node apply opp to left and right node
		auto left_res = left_node->execute();
		if (left_res.is_exception() || left_res.is_nan()) return left_res;
		res.set_value(opperation(left_res.unwrap(), res.unwrap()));
		return res;
	}

private:
	std::shared_ptr<AssetLambdaNode> left_node = nullptr;
	NonNullSharedPtr<AssetLambdaRead> right_read;
	AgisOperation opperation;
};


//============================================================================
class AssetLambdaFilter : public AssetLambdaNode {
public:
	AssetLambdaFilter(
		NonNullSharedPtr<AssetLambdaNode> const left_node_,
		AssetFilterRange const filter_range_
	) : left_node(left_node_)
	{
		this->filter = filter_range_.get_filter();
	}
	~AssetLambdaFilter() {}
	
	AgisResult<double> execute() const override {
		// execute left node to get value
		auto res = left_node->execute();
		if (res.is_exception() || res.is_nan()) return res;

		// apply filter to value
		bool res_bool = filter(res.unwrap());
		if (!res_bool) res.set_value(AGIS_NAN);
		return res;
	}

private:
	NonNullSharedPtr<AssetLambdaNode> left_node;
	std::function<bool(double)> filter;
};


//============================================================================
class ExchangeNode : public ExpressionNode<NonNullSharedPtr<Exchange>> {
public:
	ExchangeNode(NonNullSharedPtr<Exchange> exchange_)
		: exchange(exchange_) {
		if (this->exchange->get_asset_count() == 0) {
			AGIS_THROW("Exchange must have at least one asset");
		}
	}
	~ExchangeNode() {}
	NonNullSharedPtr<Exchange> evaluate() const override {
		return this->exchange;
	}

private:
	NonNullSharedPtr<Exchange> exchange;

};


//============================================================================
class ExchangeViewNode : public ValueReturningStatementNode<ExchangeView> {

public:
	ExchangeViewNode(
		NonNullSharedPtr<ExchangeNode> exchange_node_,
		NonNullSharedPtr<AssetLambdaOpp> asset_lambda_op_) :
		exchange_node(exchange_node),
		asset_lambda_op(asset_lambda_op_),
		assets(exchange_node->evaluate()->get_assets())
	{
		this->exchange = exchange_node->evaluate().get().get();
	}
	~ExchangeViewNode() {}

	// Factory function for creating AST nodes of various types
	template <typename NodeType, typename... Args>
	std::shared_ptr<AssetLambdaRead> create_asset_lambda_read(Args&&... args) {
		return std::make_shared<AssetLambdaRead>(
			this->asset,
			std::forward<Args>(args)...);
	}

	ExchangeView execute() const override {
		ExchangeView exchange_view(exchange, this->assets.size());
		auto& view = exchange_view.view;

		for (auto& asset : this->assets)
		{
			if (!asset || !asset->__in_exchange_view) continue;
			if (!asset->__is_streaming)
			{
				if (false) throw std::runtime_error("invalid asset found");
				continue;
			}
			auto val = this->asset_lambda_op->execute(asset);
			if (val.is_exception() && !false)
			{
				continue;
			}
			auto v = val.unwrap();
			view.emplace_back(asset->get_asset_index(), v);
		}
		if (view.size() == 1) { return exchange_view; }
		//exchange_view.sort(number_assets, query_type);
		return exchange_view;
	}

private:
	Exchange* exchange;
	std::vector<std::shared_ptr<Asset>> const& assets;
	NonNullSharedPtr<ExchangeNode> exchange_node;
	NonNullSharedPtr<AssetLambdaOpp> asset_lambda_op;
};