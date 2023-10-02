#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <vector>
#include <memory>

#include "AgisErrors.h"
#include "AgisPointers.h"
#include "AgisStrategy.h"

import Asset;

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
	virtual ~ExpressionNode() {}
	virtual T evaluate() const = 0;
};


//============================================================================
class StatementNode : public ASTNode {
public:
	virtual ~StatementNode() {}
	virtual void execute() = 0;
};


//============================================================================
template <typename ExpressionReturnType>
class ValueReturningStatementNode : public ASTNode {
public:
	virtual ~ValueReturningStatementNode() {}
	virtual ExpressionReturnType execute() = 0;
};


enum class AssetLambdaType {
	READ,		///< Asset lambda read opp reads data from an asset at specific column and relative index
	OPP,		///< Asset lambda opp applies arithmatic opperation to two asset lambda nodes
	OBSERVE,	///< Asset lambda observe opp reads data from an asset observer
	LOGICAL		///< Asset lambda logical opp compares asset lambda nodes to return a boolean value
};


//============================================================================
class AbstractAssetLambdaNode : public ValueReturningStatementNode<AgisResult<double>> {
public:
	/**
	 * @brief AbstractAssetLambdaNode is a node in a chain of execution steps used to create signals
	 * from an asset at the current moment in time.
	 * @param asset_lambda_type_ the type of asset lambda node
	*/
	AbstractAssetLambdaNode(AssetLambdaType asset_lambda_type_) :
		asset_lambda_type(asset_lambda_type_){};
	virtual ~AbstractAssetLambdaNode() {}

	/**
	 * @brief prevent AbstractAssetLambdaNode from being executed directly
	*/
	AgisResult<double> execute() override { return AgisResult<double>(AGIS_EXCEP("not impl")); };
	
	/**
	 * @brief pure virtual function that exexutes the asset lambda node on the given asset
	*/
	virtual AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const = 0;
		
	/**
	 * @brief get the number of warmup periods required for the asset lambda node
	*/
	size_t get_warmup() const { return this->warmup; };

	/**
	 * @brief set the type of asset lambda node
	*/
	void set_type(AssetLambdaType type) { this->asset_lambda_type = type; }
	
	/**
	 * @brief get the type of asset lambda node
	*/
	AssetLambdaType get_type() { return this->asset_lambda_type; }
protected:
	/**
	 * @brief the minimum number of rows needed to execute the asset lambda node. Max value
	 * of all child nodes warmup values.
	*/
	size_t warmup = 0;

	/**
	 * @brief the type of asset lambda node
	*/
	AssetLambdaType asset_lambda_type;
};



//============================================================================
class AbstractAssetObserve : public AbstractAssetLambdaNode {
public:
	//============================================================================
	AbstractAssetObserve(
		std::string observer_name_,
		int index_ = 0
	) : 
		observer_name(observer_name_),
		index(index_),
		AbstractAssetLambdaNode(AssetLambdaType::OBSERVE)
	{}


	//============================================================================
	AGIS_API AgisResult<bool> set_warmup(const Exchange* exchange);


	//============================================================================
	AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const override {
		return asset->get_asset_observer_result(this->observer_name);
	};

private:
	std::string observer_name;
	int index;
};


//============================================================================
class AbstractAssetLambdaRead : public AbstractAssetLambdaNode {
public:
	//============================================================================
	AbstractAssetLambdaRead(
		std::function<AgisResult<double>(std::shared_ptr<const Asset> const&)> func_,
		size_t warmup_ = 0
	) : func(func_),
		AbstractAssetLambdaNode(AssetLambdaType::READ)
	{
		this->warmup = warmup_;
	}


	//============================================================================
	AbstractAssetLambdaRead(std::string col, int index) : AbstractAssetLambdaNode(AssetLambdaType::READ)
	{
		this->col = col;
		this->index = index;
		this->warmup = static_cast<size_t>(abs(index));
	}


	//============================================================================
	~AbstractAssetLambdaRead() {}


	//============================================================================
	std::optional<std::string> get_col() {
		return this->col;
	}


	//============================================================================
	void set_col_index_lambda(size_t col_index) {
		auto l = [=](std::shared_ptr<const Asset> const& asset) -> AgisResult<double> {
			return asset->get_asset_feature(col_index, index.value());
		};
		this->func = l;
	}


	//============================================================================
	AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const override {
		return this->func(asset);
	};

private:
	std::optional<std::string> col;
	std::optional<int> index;
	std::function<AgisResult<double>(std::shared_ptr<const Asset> const&)> func;
};


//============================================================================
class AbstractAssetLambdaLogical : public AbstractAssetLambdaNode {
public:
	using AgisLogicalRightVal = std::variant<double, std::unique_ptr<AbstractAssetLambdaNode>>;
	AbstractAssetLambdaLogical(
		std::unique_ptr<AbstractAssetLambdaNode> left_node_,
		AgisLogicalType logical_type_,
		AgisLogicalRightVal right_node_,
		bool numeric_cast = false
	):
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
	AgisResult<double> execute(std::shared_ptr<const Asset> const& asset) const override {
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

private:
	AgisLogicalOperation logical_compare;
	NonNullUniquePtr<AbstractAssetLambdaNode> left_node;
	AgisLogicalType logical_type;
	AgisLogicalRightVal right_node;
	bool numeric_cast = false;
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
		opperation(opperation_),
		AbstractAssetLambdaNode(AssetLambdaType::OPP)
	{
		this->warmup = this->right_read->get_warmup();
		if (this->left_node != nullptr) {
			this->warmup = std::max(this->warmup, this->left_node->get_warmup());
		}
	}
	~AbstractAssetLambdaOpp() {}


	//============================================================================
	[[nodiscard]] AgisResult<bool> build_node(AbstractAssetLambdaNode* node, const Exchange* exchange) {
		assert(node);
		auto type = node->get_type();
		switch (type) {
			case AssetLambdaType::READ: {
				auto left_read = static_cast<AbstractAssetLambdaRead*>(node);
				auto str_col = left_read->get_col();
				if (str_col.has_value()) {
					auto col_res = exchange->get_column_index(str_col.value());
					if (col_res.is_exception()) return AgisResult<bool>(col_res.get_exception());
					left_read->set_col_index_lambda(col_res.unwrap());
				}
				break;
			}
			case AssetLambdaType::OBSERVE: {
				auto left_read = static_cast<AbstractAssetObserve*>(node);
				AGIS_DO(left_read->set_warmup(exchange));
				break;
			}
			case AssetLambdaType::OPP: {
				auto left_opp = static_cast<AbstractAssetLambdaOpp*>(node);
				auto res = left_opp->build(exchange);
				if (res.is_exception()) return AgisResult<bool>(res.get_exception());
				break;
			}
			case AssetLambdaType::LOGICAL: {
				break;
			}
			default: {
				return AgisResult<bool>(AGIS_EXCEP("invalid asset lambda type"));
			}
		}
		return AgisResult<bool>(true);
	}


	//============================================================================
	AgisResult<bool> build(const Exchange* exchange) {
		// search through all asset lambda nodes and set the size_t column 
		// index for all read opps.
		if (this->left_node) {
			AGIS_DO(this->build_node(this->left_node.get(), exchange));
		}
		// set right node 
		auto p = static_cast<AbstractAssetLambdaNode*>(this->right_read.get().get());
		AGIS_DO(this->build_node(p, exchange));
		return AgisResult<bool>(true);
	}


	//============================================================================
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
class AbstractExchangeNode : public ExpressionNode<const Exchange*> {
public:
	//============================================================================
	AbstractExchangeNode(NonNullSharedPtr<Exchange> exchange_)
		: exchange(exchange_.get().get()) {
		if (this->exchange->get_asset_count() == 0) {
			AGIS_THROW("Exchange must have at least one asset");
		}
	}

	//============================================================================
	AbstractExchangeNode(ExchangePtr const exchange_)
		: exchange(exchange_.get()) {
		if (this->exchange->get_asset_count() == 0) {
			AGIS_THROW("Exchange must have at least one asset");
		}
	}

	//============================================================================
	~AbstractExchangeNode() {}
	const Exchange* evaluate() const override {
		return this->exchange;
	}

private:
	const Exchange* exchange; // Store a const pointer to Exchange
};


//============================================================================
class AbstractExchangeViewNode : public ValueReturningStatementNode<AgisResult<bool>>{

public:
	//============================================================================
	AbstractExchangeViewNode(
		std::unique_ptr<AbstractExchangeNode> exchange_node_,
		std::unique_ptr<AbstractAssetLambdaOpp> asset_lambda_op_) :
		exchange_node(std::move(exchange_node_)),
		asset_lambda_op(std::move(asset_lambda_op_))
	{
		// extract exchange from node, copy asset pointers into the node
		this->exchange = exchange_node->evaluate();
		for (auto& asset : this->exchange->get_assets()) {
			this->assets.push_back(asset);
		}

		// for all read opps get size_t col index and set lambda func
		auto res = this->asset_lambda_op->build(this->exchange);
		if (res.is_exception()) throw std::runtime_error(res.get_exception());
		// build exchange view used for all opps
		this->exchange_view = ExchangeView(
			this->exchange, 
			this->exchange->get_asset_count(),
			false
		);
		// set the minimum warmup for all opps
		this->warmup = this->asset_lambda_op->get_warmup();
	}

	//============================================================================
	~AbstractExchangeViewNode() {}


	//============================================================================
	ExchangeView get_view() {
		return this->exchange_view;
	}


	//============================================================================
	size_t size() {
		return this->exchange_view.view.size();
	}


	//============================================================================
	size_t get_warmup() {
		return this->warmup;
	}

	/**
	 * @brief remove all asset ids from the excahgne view that are not in the given vector
	 * @param ids_keep vector of asset ids to keep
	*/
	AGIS_API void apply_asset_index_filter(std::vector<size_t> const& index_keep);


	//============================================================================
	AGIS_API AgisResult<bool> execute() override;

private:
	ExchangeView exchange_view;
	const Exchange* exchange;
	std::vector<AssetPtr> assets;
	NonNullUniquePtr<AbstractExchangeNode> exchange_node;
	NonNullUniquePtr<AbstractAssetLambdaOpp> asset_lambda_op;
	size_t warmup = 0;
};


//============================================================================
class AbstractSortNode : ValueReturningStatementNode<AgisResult<ExchangeView>> {
public:
	//============================================================================
	AbstractSortNode(
		std::unique_ptr<AbstractExchangeViewNode> ev_,
		int N_,
		ExchangeQueryType query_type_
	) : 
		ev(std::move(ev_))	{
		this->N = (N_ == -1) ? ev->size() : static_cast<size_t>(N_);
		this->query_type = query_type_;
	}


	//============================================================================
	size_t get_warmup() {
		return this->ev->get_warmup();
	}


	//============================================================================
	AgisResult<ExchangeView> execute() override {
		auto res = this->ev->execute();
		if(res.is_exception()) return AgisResult<ExchangeView>(res.get_exception());
		auto view = this->ev->get_view();
		view.clean();
		view.sort(N, query_type);
		return AgisResult<ExchangeView>(view);
	}

private:
	NonNullUniquePtr<AbstractExchangeViewNode> ev;
	size_t N;
	ExchangeQueryType query_type;
};


//============================================================================
class AbstractGenAllocationNode : public ValueReturningStatementNode<AgisResult<ExchangeView>> {
public:
	//============================================================================
	AbstractGenAllocationNode(
		std::unique_ptr<AbstractSortNode> sort_node_,
		ExchangeViewOpp ev_opp_type_,
		double target,
		std::optional<double> ev_opp_param
	) :
		sort_node(std::move(sort_node_)),
		ev_opp_type(ev_opp_type_),
		target(target),
		ev_opp_param(ev_opp_param)
	{}


	//============================================================================
	AgisResult<ExchangeView> execute() override {
		auto view_res = sort_node->execute();
		if (view_res.is_exception()) return view_res;
		ExchangeView view = view_res.unwrap();
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
		return AgisResult<ExchangeView>(std::move(view));
	}

	size_t get_warmup() {
		return this->sort_node->get_warmup();
	}

private:
	NonNullUniquePtr<AbstractSortNode> sort_node;
	ExchangeViewOpp ev_opp_type;
	double target;
	std::optional<double> ev_opp_param;
};


//============================================================================
class AbstractStrategyAllocationNode : public ValueReturningStatementNode<AgisResult<bool>> {
public:
	//============================================================================
	AbstractStrategyAllocationNode(
		AgisStrategy* strategy_,
		std::unique_ptr<AbstractGenAllocationNode> gen_alloc_node_,
		double epsilon_,
		bool clear_missing_,
		std::optional<TradeExitPtr> exit_ = std::nullopt,
		AllocType alloc_type_ = AllocType::PCT
	) :
		strategy(strategy_),
		gen_alloc_node(std::move(gen_alloc_node_)),
		epsilon(epsilon_),
		clear_missing(clear_missing_),
		alloc_type(alloc_type_)
	{}


	//============================================================================
	size_t get_warmup() {
		return this->gen_alloc_node->get_warmup();
	}


	//============================================================================
	AgisResult<bool> execute() override {
		auto ev_res = this->gen_alloc_node->execute();
		if (ev_res.is_exception()) return AgisResult<bool>(ev_res.get_exception());
		auto ev = ev_res.unwrap();
		this->strategy->strategy_allocate(
			ev,
			this->epsilon,
			this->clear_missing,
			this->exit,
			this->alloc_type
		);
		return AgisResult<bool>(true);
	}


private:
	AgisStrategy* strategy;
	NonNullUniquePtr<AbstractGenAllocationNode> gen_alloc_node;
	double epsilon;
	bool clear_missing;
	std::optional<TradeExitPtr> exit = std::nullopt;
	AllocType alloc_type = AllocType::PCT;
};


//============================================================================
AGIS_API std::unique_ptr<AbstractAssetLambdaRead> create_asset_lambda_read(std::string col, int index);

//============================================================================
AGIS_API std::unique_ptr<AbstractAssetLambdaOpp> create_asset_lambda_opp(
	std::unique_ptr<AbstractAssetLambdaNode>& left_node,
	std::unique_ptr<AbstractAssetLambdaRead>& right_read,
	AgisOpperationType opperation
);


//============================================================================
AGIS_API std::unique_ptr<AbstractExchangeNode> create_exchange_node(
	ExchangePtr const exchange
);

//============================================================================
AGIS_API std::unique_ptr<AbstractExchangeViewNode> create_exchange_view_node(
	std::unique_ptr<AbstractExchangeNode>& exchange_node,
	std::unique_ptr<AbstractAssetLambdaOpp>& asset_lambda_op
);


//============================================================================
AGIS_API std::unique_ptr<AbstractSortNode> create_sort_node(
	std::unique_ptr<AbstractExchangeViewNode>& ev,
	int N,
	ExchangeQueryType query_type
);


//============================================================================
AGIS_API std::unique_ptr<AbstractGenAllocationNode> create_gen_alloc_node(
	std::unique_ptr<AbstractSortNode>& sort_node,
	ExchangeViewOpp ev_opp_type,
	double target,
	std::optional<double> ev_opp_param
);


//============================================================================
AGIS_API std::unique_ptr<AbstractStrategyAllocationNode> create_strategy_alloc_node(
	AgisStrategy* strategy_,
	std::unique_ptr<AbstractGenAllocationNode>& gen_alloc_node_,
	double epsilon_,
	bool clear_missing_,
	std::optional<TradeExitPtr> exit_ = std::nullopt,
	AllocType alloc_type_ = AllocType::PCT);