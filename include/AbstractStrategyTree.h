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

namespace Agis {
	class Asset;
	class Future;
	class FutureTable;
}


class ASTNode;
class AssetNode;

class AbstractAssetLambdaNode;
class AbstractAssetLambdaRead;
class AbstractAssetLambdaChain;
class AbstractSortNode;

typedef NonNullSharedPtr<Asset> NonNullAssetPtr;
typedef std::shared_ptr<FutureTable> FutureTablePtr;
typedef std::shared_ptr<Future> FuturePtr;

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
	virtual size_t get_warmup() const = 0;
};


enum class AssetLambdaType {
	READ,		///< Asset lambda read opp reads data from an asset at specific column and relative index
	OPP,		///< Asset lambda opp applies arithmatic opperation to two asset lambda nodes
	OBSERVE,	///< Asset lambda observe opp reads data from an asset observer
	LOGICAL		///< Asset lambda logical opp compares asset lambda nodes to return a boolean value
};


//============================================================================
class AbstractAssetLambdaNode : public ValueReturningStatementNode<std::expected<double, AgisErrorCode>> {
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
	std::expected<double, AgisErrorCode> execute() override { AGIS_NOT_IMPL };
	
	/**
	 * @brief pure virtual function that exexutes the asset lambda node on the given asset
	*/
	virtual std::expected<double, AgisErrorCode> execute(std::shared_ptr<const Asset> const& asset) const = 0;
		
	/**
	 * @brief get the number of warmup periods required for the asset lambda node
	*/
	size_t get_warmup() const override { return this->warmup; };

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
	std::expected<double, AgisErrorCode> execute(std::shared_ptr<const Asset> const& asset) const override;

private:
	std::string observer_name;
	int index;
};


//============================================================================
class AbstractAssetLambdaRead : public AbstractAssetLambdaNode {
public:
	//============================================================================
	AbstractAssetLambdaRead(
		std::function<std::expected<double, AgisErrorCode>(std::shared_ptr<const Asset> const&)> func_,
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
	void set_col_index_lambda(size_t col_index);


	//============================================================================
	std::expected<double, AgisErrorCode> execute(std::shared_ptr<const Asset> const& asset) const override {
		return this->func(asset);
	};

private:
	std::optional<std::string> col;
	std::optional<int> index;
	std::function<std::expected<double, AgisErrorCode>(std::shared_ptr<const Asset> const&)> func;
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
	);

	//============================================================================
	std::expected<double, AgisErrorCode> execute(std::shared_ptr<const Asset> const& asset) const override;

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
	std::expected<double, AgisErrorCode> execute(std::shared_ptr<const Asset> const& asset) const override {
		// check if right opp is null or nan
		auto res = right_read->execute(asset);
		if (!res.has_value() || std::isnan(res.value())) return res;

		// if no left node return opp applied right node
		if(left_node == nullptr) return std::expected<double, AgisErrorCode>(
			opperation(0.0f, res.value())
		);

		// if left node apply opp to left and right node
		auto left_res = left_node->execute(asset);
		if (!left_res.has_value() || std::isnan(res.value())) return left_res;
		return opperation(left_res.value(), res.value());
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

	//============================================================================
	const Exchange* evaluate() const override {
		return this->exchange;
	}

private:
	const Exchange* exchange;
};


enum class TableExtractMethod : uint16_t {
	FRONT = 0
};


//============================================================================
class AbstractFutureTableNode : public ExpressionNode<std::expected<AssetPtr, AgisErrorCode>> {
public:
	//============================================================================
	AbstractFutureTableNode(
		std::shared_ptr<AbstractExchangeNode> exchange_node_,
		std::string contract_id,
		TableExtractMethod extract_method_
	);


	//============================================================================
	std::expected<AssetPtr,AgisErrorCode> evaluate() const override;


	//============================================================================
	const Exchange* get_exchange() const {
		return this->exchange;
	}

private:
	FutureTablePtr table;
	const Exchange* exchange;
	std::string contract_id;
	TableExtractMethod extract_method;
};


//============================================================================
class AbstractTableViewNode : public ValueReturningStatementNode<std::expected<ExchangeView, AgisErrorCode>> {

public:
	//============================================================================
	AbstractTableViewNode(
		std::shared_ptr<AbstractFutureTableNode> table_node,
		std::unique_ptr<AbstractAssetLambdaOpp> asset_lambda_op_) :
		asset_lambda_op(std::move(asset_lambda_op_))
	{
		this->table_nodes.push_back(table_node);
		this->warmup = this->asset_lambda_op->get_warmup();
		auto res = this->asset_lambda_op->build(table_node->get_exchange());
		if (res.is_exception()) throw std::runtime_error(res.get_exception());
	}

	//============================================================================
	AGIS_API [[nodiscard]] std::expected<bool, AgisErrorCode> add_asset_table(std::shared_ptr<AbstractFutureTableNode> table_node);

	//============================================================================
	std::expected<ExchangeView, AgisErrorCode> execute() override;

	//============================================================================
	std::expected<bool, AgisErrorCode> evaluate_asset(AssetPtr const& asset, ExchangeView& view) const noexcept;

	//============================================================================
	size_t get_warmup() const override {
		return this->warmup;
	}

private:
	std::vector<std::shared_ptr<AbstractFutureTableNode>> table_nodes;
	NonNullUniquePtr<AbstractAssetLambdaOpp> asset_lambda_op;
	size_t warmup = 0;
};


//============================================================================
class AbstractExchangeViewNode : public ValueReturningStatementNode<std::expected<bool, AgisErrorCode>>{

public:
	//============================================================================
	AbstractExchangeViewNode(
		std::shared_ptr<AbstractExchangeNode> exchange_node_,
		std::unique_ptr<AbstractAssetLambdaOpp> asset_lambda_op_) :
		exchange_node(exchange_node_),
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
	size_t get_warmup() const override{
		return this->warmup;
	}

	/**
	 * @brief remove all asset ids from the excahgne view that are not in the given vector
	 * @param ids_keep vector of asset ids to keep
	*/
	AGIS_API void apply_asset_index_filter(std::vector<size_t> const& index_keep);


	//============================================================================
	AGIS_API std::expected<bool, AgisErrorCode> execute() override;

private:
	ExchangeView exchange_view;
	const Exchange* exchange;
	std::vector<AssetPtr> assets;
	NonNullSharedPtr<AbstractExchangeNode> exchange_node;
	NonNullUniquePtr<AbstractAssetLambdaOpp> asset_lambda_op;
	size_t warmup = 0;
};


//============================================================================
class AbstractSortNode : ValueReturningStatementNode<std::expected<ExchangeView,AgisErrorCode>> {
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
	size_t get_warmup() const override{
		return this->ev->get_warmup();
	}


	//============================================================================
	std::expected<ExchangeView, AgisErrorCode> execute() override {
		auto res = this->ev->execute();
		if(!res.has_value()) return std::unexpected<AgisErrorCode>(res.error());
		auto view = this->ev->get_view();
		view.clean();
		view.sort(N, query_type);
		return view;
	}

private:
	NonNullUniquePtr<AbstractExchangeViewNode> ev;
	size_t N;
	ExchangeQueryType query_type;
};


//============================================================================
class AbstractGenAllocationNode : public ValueReturningStatementNode<std::expected<ExchangeView, AgisErrorCode>> {
public:
	//============================================================================
	using SourceNode = std::variant<
		std::unique_ptr<AbstractTableViewNode>,
		std::unique_ptr<AbstractSortNode>>;

	AGIS_API ~AbstractGenAllocationNode() = default;
	AbstractGenAllocationNode(
		SourceNode sort_node_,
		ExchangeViewOpp ev_opp_type_,
		double target,
		std::optional<double> ev_opp_param
	) :
		source_node(std::move(sort_node_)),
		ev_opp_type(ev_opp_type_),
		target(target),
		ev_opp_param(ev_opp_param)
	{}

	//============================================================================
    std::expected<ExchangeView, AgisErrorCode> vist_execute() {
		return std::visit([](auto&& option) {
			if constexpr (std::is_same_v<std::decay_t<decltype(option)>, std::unique_ptr<AbstractTableViewNode>>) {
				return option->execute();
			}
			else if constexpr (std::is_same_v<std::decay_t<decltype(option)>, std::unique_ptr<AbstractSortNode>>) {
				return option->execute();
			}
			else {
				return std::unexpected<AgisErrorCode>(AgisErrorCode::INVALID_CONFIGURATION);
			}
			}, source_node
		);
    }

	//============================================================================
	size_t vist_warmup() const {
		return std::visit([](auto&& option) {
			if constexpr (std::is_same_v<std::decay_t<decltype(option)>, std::unique_ptr<AbstractTableViewNode>>) {
				return option->get_warmup();
			}
			else if constexpr (std::is_same_v<std::decay_t<decltype(option)>, std::unique_ptr<AbstractSortNode>>) {
				return option->get_warmup();
			}
			else {
				return std::unexpected<AgisErrorCode>(AgisErrorCode::INVALID_CONFIGURATION);
			}
			}, source_node
		);
	}

	//============================================================================
	std::expected<ExchangeView, AgisErrorCode> execute() override {
		auto view_res = this->vist_execute();
		if (!view_res.has_value()) return view_res;
		ExchangeView view = std::move(view_res.value());
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
		auto res = this->apply_scale(&view)
			.and_then([&](ExchangeView* v) {return this->apply_vol_target(v); }
		);
		if(!res.has_value()) return std::unexpected<AgisErrorCode>(res.error());
		return std::move(view);
	}

	AGIS_API void set_ev_scaler_type(ExchangeViewScaler ev_scaler_type_) {
		this->ev_scaler_type = ev_scaler_type_;
	}

	AGIS_API void set_vol_target(double vol_target_) {
		this->vol_target = vol_target_;
	}

	std::expected<ExchangeView*, AgisErrorCode> apply_vol_target(ExchangeView* v) {
		if (!this->vol_target.has_value()) return v;
		auto res = v->vol_target(this->vol_target.value());
		if (res.is_exception()) return std::unexpected<AgisErrorCode>(AgisErrorCode::INVALID_CONFIGURATION);
		return v;
	}

	std::expected<ExchangeView*, AgisErrorCode> apply_scale(ExchangeView* v) {
		if (this->ev_scaler_type == ExchangeViewScaler::NONE) return v;
		auto res = v->allocation_scale(this->ev_scaler_type);
		if (!res.has_value()) return std::unexpected<AgisErrorCode>(res.error());
		return v;
	}

	size_t get_warmup() const override {
		return this->vist_warmup();
	}

private:
	SourceNode source_node;
	ExchangeViewOpp ev_opp_type;
	ExchangeViewScaler ev_scaler_type = ExchangeViewScaler::NONE;
	double target;
	std::optional<double> ev_opp_param;
	std::optional<double> vol_target;
};


//============================================================================
class AbstractStrategyAllocationNode : public ValueReturningStatementNode<std::expected<bool,AgisErrorCode>> {
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
	size_t get_warmup() const override {
		return this->gen_alloc_node->get_warmup();
	}


	//============================================================================
	std::expected<bool, AgisErrorCode> execute() override {
		auto ev_res = this->gen_alloc_node->execute();
		if (!ev_res.has_value()) return std::unexpected<AgisErrorCode>(ev_res.error());
		auto& ev = ev_res.value();
		this->strategy->strategy_allocate(
			ev,
			this->epsilon,
			this->clear_missing,
			this->exit,
			this->alloc_type
		);
		return true;
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
AGIS_API std::unique_ptr<AbstractAssetLambdaOpp> create_asset_lambda_opp_r(
	std::unique_ptr<AbstractAssetLambdaNode>&& left_node,
	std::unique_ptr<AbstractAssetLambdaRead>&& right_read,
	AgisOpperationType opperation
);


//============================================================================
AGIS_API std::shared_ptr<AbstractExchangeNode> create_exchange_node(
	ExchangePtr const exchange
);


//============================================================================
AGIS_API std::shared_ptr<AbstractFutureTableNode> create_future_table_node(
	std::shared_ptr<AbstractExchangeNode> exchange_node_,
	std::string contract_id,
	TableExtractMethod extract_method_
);


//============================================================================
AGIS_API std::unique_ptr<AbstractTableViewNode> create_future_view_node(
	std::shared_ptr<AbstractFutureTableNode> table_node,
	std::unique_ptr<AbstractAssetLambdaOpp>& asset_lambda_op_
);

//============================================================================
AGIS_API std::unique_ptr<AbstractExchangeViewNode> create_exchange_view_node(
	std::shared_ptr<AbstractExchangeNode>& exchange_node,
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
AGIS_API std::unique_ptr<AbstractGenAllocationNode> create_table_gen_alloc_node(
	std::unique_ptr<AbstractTableViewNode>& sort_node,
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