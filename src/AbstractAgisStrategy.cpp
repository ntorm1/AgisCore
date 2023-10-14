#include "pch.h"
#include "AbstractAgisStrategy.h"

#include "Asset/Asset.h"

using namespace Agis;

//============================================================================
void AbstractAgisStrategy::next()
{
	auto& ev_lambda_ref = *this->ev_lambda_struct;
	ExchangeView ev;
	AGIS_TRY(
		ev = ev_lambda_ref.exchange_view_labmda(
			ev_lambda_ref.asset_lambda,
			ev_lambda_ref.exchange,
			ev_lambda_ref.query_type,
			ev_lambda_ref.N
		);
	);

	auto& strat_alloc_ref = *ev_lambda_ref.strat_alloc_struct;
	switch (this->ev_opp_type)
	{
		case ExchangeViewOpp::UNIFORM: {
			ev.uniform_weights(strat_alloc_ref.target);
			break;
		}
		case ExchangeViewOpp::LINEAR_INCREASE: {
			ev.linear_increasing_weights(strat_alloc_ref.target);
			break;
		}
		case ExchangeViewOpp::LINEAR_DECREASE: {
			ev.linear_decreasing_weights(strat_alloc_ref.target);
			break;
		}
		case ExchangeViewOpp::CONDITIONAL_SPLIT: {
			ev.conditional_split(strat_alloc_ref.target, this->ev_opp_param.value());
			break;
		}
		case ExchangeViewOpp::UNIFORM_SPLIT: {
			ev.uniform_split(strat_alloc_ref.target);
			break;
		}
		case ExchangeViewOpp::CONSTANT: {
			auto c = this->ev_opp_param.value() * strat_alloc_ref.target;
			ev.constant_weights(c, this->trades);
			break;
		}
		default: {
			throw std::runtime_error("invalid exchange view operation");
		}
	}
	AGIS_TRY(
		this->strategy_allocate(
			ev,
			strat_alloc_ref.epsilon,
			strat_alloc_ref.clear_missing,
			strat_alloc_ref.trade_exit,
			strat_alloc_ref.alloc_type
		);
	)
}


//============================================================================
void AbstractAgisStrategy::build()
{
	if (!ev_lambda_struct.has_value()) {
		throw std::runtime_error(this->get_strategy_id() + " missing abstract lambda strategy");
	}

	ExchangePtr exchange = ev_lambda_struct.value().exchange;
	auto res = this->exchange_subscribe(exchange->get_exchange_id());

	// validate exchange subscription
	if (res.is_exception()) throw res.get_exception();

	// validate beta hedge
	if (this->apply_beta_hedge || this->apply_beta_scale)
	{
		auto market_asset = exchange->__get_market_asset();
		if (market_asset.is_exception()) throw market_asset.get_exception();
	}

	// set the strategy warmup period
	this->warmup = this->ev_lambda_struct.value().warmup;

	// set the strategy target leverage
	this->alloc_target = this->ev_lambda_struct.value().strat_alloc_struct.value().target;
}


//============================================================================
AgisResult<bool> AbstractAgisStrategy::extract_ev_lambda()
{
	this->ev_lambda_struct = this->ev_lambda();

	if (!this->ev_lambda_struct.has_value()) {
		return AgisResult<bool>(AGIS_EXCEP("missing ev lambda struct"));
	}

	auto& ev_lambda_ref = *this->ev_lambda_struct;

	if (!ev_lambda_ref.exchange) {
		return AgisResult<bool>(AGIS_EXCEP("missing exchange"));
	}

	// set ev alloc type. Defined how to set portfolio weights
	auto& strat_alloc_ref = *ev_lambda_ref.strat_alloc_struct;
	auto res = str_to_ev_opp(strat_alloc_ref.ev_opp_type);
	if (res.is_exception()) return AgisResult<bool>(res.get_exception());
	this->ev_opp_type = res.unwrap();

	// set ev extra param if needed
	if (this->ev_opp_type == ExchangeViewOpp::CONDITIONAL_SPLIT ||
		this->ev_opp_type == ExchangeViewOpp::CONSTANT)
	{
		std::optional<double> val = strat_alloc_ref.ev_extra_opp.value();
		if (!val.has_value())
		{
			return AgisResult<bool>(AGIS_EXCEP("exchange view opperation expected extra ev parameters"));
		}
		else
		{
			this->ev_opp_param = val;
		}
	}

	// set the target type
	this->alloc_type_target = strat_alloc_ref.alloc_type_target;
	if (this->alloc_type_target == AllocTypeTarget::VOL && !this->get_max_leverage().has_value()) {
		return AgisResult<bool>(AGIS_EXCEP("target vol must have max leverage set"));
	}
	this->build();
	return AgisResult<bool>(true);
}


//============================================================================
void AbstractAgisStrategy::restore(fs::path path)
{
}


//============================================================================
std::string trading_window_to_str(std::optional<TradingWindow> window_op) {
	if (!window_op.has_value()) { return ""; }
	std::ostringstream codeStream;
	auto window = window_op.value();
	codeStream << "TradingWindow(";
	codeStream << "std::make_pair(TimePoint{" << window.first.hour << ", " << window.first.minute << "}, ";
	codeStream << "TimePoint{" << window.second.hour << ", " << window.second.minute << "})";
	codeStream << ")";

	return codeStream.str();
}


//============================================================================
void AbstractAgisStrategy::code_gen(fs::path strat_folder)
{
	if (!this->ev_lambda_struct.has_value())
	{
		AGIS_THROW("Abstract strategy has not been built yet");
	}
	auto exchange_id = this->ev_lambda_struct.value().exchange->get_exchange_id();
	auto portfolio_id = this->get_portfolio_id();
	auto warmup = this->ev_lambda_struct.value().warmup;
	auto& ev_lambda_ref = *this->ev_lambda_struct;
	auto& strat_alloc_ref = *ev_lambda_ref.strat_alloc_struct;

	std::string strategy_header = R"(#pragma once

#ifdef AGISSTRATEGY_EXPORTS // This should be defined when building the DLL
#  define AGIS_STRATEGY_API __declspec(dllexport)
#else
#  define AGIS_STRATEGY_API __declspec(dllimport)
#endif

// the following code is generated from an abstract strategy flow graph.
// EDIT IT AT YOUR OWN RISK 
#include "AgisStrategy.h"

class {STRATEGY_ID}_CPP : public AgisStrategy {
public:
	AGIS_STRATEGY_API {STRATEGY_ID}_CPP (
        PortfolioPtr const portfolio_
    ) : AgisStrategy("{STRATEGY_ID}_CPP", portfolio_, {ALLOC}) {
		this->strategy_type = AgisStrategyType::CPP;
		this->trading_window = {TRADING_WINDOW};
	};

    AGIS_STRATEGY_API inline static std::unique_ptr<AgisStrategy> create_instance(
        PortfolioPtr const& portfolio_
    ) 
	{
        return std::make_unique<{STRATEGY_ID}_CPP>(portfolio_);
    }

	AGIS_STRATEGY_API inline void reset() override {}

	AGIS_STRATEGY_API void build() override;

	AGIS_STRATEGY_API void next() override;

private:
	ExchangeViewOpp ev_opp_type = ExchangeViewOpp::{EV_OPP_TYPE};
	ExchangePtr exchange = nullptr;
	size_t warmup = {WARMUP};
};
)";

	// Replace the placeholder with the EV_OPP_TYPE value
	auto pos = strategy_header.find("{EV_OPP_TYPE}");
	strategy_header.replace(pos, 13, ev_opp_to_str(this->ev_opp_type));

	// Replace the allocation amount
	pos = strategy_header.find("{ALLOC}");
	strategy_header.replace(pos, 7, std::to_string(this->get_allocation()));

	// Replace the warmup 
	pos = strategy_header.find("{WARMUP}");
	strategy_header.replace(pos, 8, std::to_string(warmup));

	// Replace the trading_window
	pos = strategy_header.find("{TRADING_WINDOW}");
	if (this->trading_window.has_value()) {
		strategy_header.replace(pos, 16, trading_window_to_str(this->trading_window.value()));
	}
	else {
		strategy_header.replace(pos, 16, "std::nullopt");
	}

	// Replace strategy class name
	std::string place_holder = "{STRATEGY_ID}";
	std::string strategy_id = this->get_strategy_id();
	str_replace_all(strategy_header, place_holder, strategy_id);

	// Insert exchange ID
	std::string exchange_str = R"("{ID}")";
	pos = exchange_str.find("{ID}");
	exchange_str.replace(pos, 4, exchange_id);

	std::string build_method =
		R"(this->exchange_subscribe("{EXCHANGE_ID}");
	this->exchange = this->get_exchange();)";
	str_replace_all(build_method, "{EXCHANGE_ID}", this->get_exchange()->get_exchange_id());

	// build the vector of asset lambdas to be used when calling next
	std::string asset_lambda = R"(std::vector<AssetLambdaScruct> operations = { )";
	int i = 0;
	for (auto& pair : ev_lambda_ref.asset_lambda)
	{
		std::string lambda_mid;
		if (pair.is_operation()) {
			lambda_mid = R"(AssetLambdaScruct(AssetLambda({OPP}, [&](const AssetPtr& asset) {
			return asset->get_asset_feature("{COL}", {INDEX});
		}),{OPP}, "{COL}", {INDEX})
)";
			auto pos = lambda_mid.find("{OPP}");
			auto& asset_operation = pair.get_asset_operation_struct();
			lambda_mid.replace(pos, 5, opp_to_str(pair.get_agis_operation()));

			str_replace_all(lambda_mid, "{COL}", asset_operation.column);
			str_replace_all(lambda_mid, "{OPP}", opp_to_str(pair.get_agis_operation()));
			str_replace_all(lambda_mid, "{INDEX}", std::to_string(asset_operation.row));
		}
		else {
			lambda_mid = pair.get_asset_filter_struct().asset_filter_range.code_gen();
		}
		// append the asset lambda str rep to the vector containing the operations
		asset_lambda = asset_lambda + lambda_mid;
		if (i < ev_lambda_ref.asset_lambda.size() - 1)
		{
			asset_lambda = asset_lambda + ", ";
		}
		else {
			asset_lambda = asset_lambda + "};";
		}
		i++;
	}
	// agis strategy next method
	std::string next_method = R"(auto next_lambda = [&operationsRef](const AssetPtr& asset) -> AgisResult<double> {			
		return asset_feature_lambda_chain(
			asset, 
			operationsRef
		);
	};
		
	auto ev = this->exchange->get_exchange_view(
		next_lambda, 
		ExchangeQueryType::{EXCHANGE_QUERY_TYPE},
		{N}
	);

	{EV_TRANSFORM}

	this->strategy_allocate(
		ev,
		{EPSILON},
		{CLEAR},
		std::nullopt,
		AllocType::{ALLOC_TYPE}
	);

	)";

	// Replace the exchange query type
	pos = next_method.find("{EXCHANGE_QUERY_TYPE}");
	next_method.replace(pos, 21, ev_query_type(ev_lambda_ref.query_type));

	pos = next_method.find("{N}");
	next_method.replace(pos, 3, std::to_string(ev_lambda_ref.N));

	auto& strat_alloc_struct = *ev_lambda_ref.strat_alloc_struct;
	// Replace epsilon
	pos = next_method.find("{EPSILON}");
	next_method.replace(pos, 9, std::to_string(strat_alloc_struct.epsilon));

	// Clear position if missing
	pos = next_method.find("{CLEAR}");
	next_method.replace(pos, 7, std::to_string(strat_alloc_struct.clear_missing));

	// Strategy allocation type
	pos = next_method.find("{ALLOC_TYPE}");
	next_method.replace(pos, 12, alloc_to_str(strat_alloc_struct.alloc_type));

	// Replace ev transform
	auto target_leverage = std::to_string(strat_alloc_ref.target);
	std::string ev_opp_str;
	if (strat_alloc_ref.ev_opp_type == "UNIFORM")
		ev_opp_str = R"(ev.uniform_weights({LEV});)";
	else if (strat_alloc_ref.ev_opp_type == "LINEAR_DECREASE")
		ev_opp_str = R"(ev.linear_decreasing_weights({LEV});)";
	else if (strat_alloc_ref.ev_opp_type == "LINEAR_INCREASE")
		ev_opp_str = R"(ev.linear_increasing_weights({LEV});)";
	pos = ev_opp_str.find("{LEV}");
	ev_opp_str.replace(pos, 5, std::to_string(strat_alloc_struct.target));
	pos = next_method.find("{EV_TRANSFORM}");
	next_method.replace(pos, 14, ev_opp_str);

	std::string strategy_source = R"(
// the following code is generated from an abstract strategy flow graph.
// EDIT IT AT YOUR OWN RISK 

#include "{STRATEGY_ID}_CPP.h"

{LAMBDA_CHAIN}

void {STRATEGY_ID}_CPP::build(){
	// set the strategies target exchanges
	{BUILD_METHOD}
	
	this->set_beta_trace({BETA_TRACE});
	this->set_beta_scale_positions({BETA_SCALE});
	this->set_beta_hedge_positions({BETA_HEDGE});
	this->set_net_leverage_trace({NET_LEV});
	this->set_step_frequency({FREQ});
};

void {STRATEGY_ID}_CPP::next(){
	if (this->exchange->__get_exchange_index() < this->warmup) { return; }

    auto& operationsRef = operations; // Create a reference to operations

	// define the lambda function the strategy will apply
	{NEXT_METHOD}
};
)";

	// Replace the placeholder with the BUILD_METHOD
	pos = strategy_source.find("{BUILD_METHOD}");
	strategy_source.replace(pos, 14, build_method);

	// Replace the placeholder with the NEXT_METHOD
	pos = strategy_source.find("{NEXT_METHOD}");
	strategy_source.replace(pos, 13, next_method);

	// Replace the lambda chain
	pos = strategy_source.find("{LAMBDA_CHAIN}");
	strategy_source.replace(pos, 14, asset_lambda);

	// Replace the lambda chain
	pos = strategy_source.find("{BETA_TRACE}");
	strategy_source.replace(pos, 12, (this->tracers.has(Tracer::BETA)) ? "true" : "false");

	// Replace the lambda chain
	pos = strategy_source.find("{BETA_SCALE}");
	strategy_source.replace(pos, 12, (apply_beta_scale) ? "true" : "false");

	// Replace the lambda chain
	pos = strategy_source.find("{BETA_HEDGE}");
	strategy_source.replace(pos, 12, (apply_beta_hedge) ? "true" : "false");

	// Replace the lambda chain
	pos = strategy_source.find("{NET_LEV}");
	strategy_source.replace(pos, 9, (this->tracers.has(Tracer::LEVERAGE)) ? "true" : "false");

	// Replace the lambda chain
	pos = strategy_source.find("{FREQ}");
	// convert the frequency to a string
	std::string freq_str = std::to_string(this->get_step_frequency());
	str_replace_all(strategy_source, "{FREQ}", freq_str);

	// Replace strategy class name
	str_replace_all(strategy_source, place_holder, strategy_id);

	auto header_path = strat_folder / (strategy_id + "_CPP.h");
	auto source_path = strat_folder / (strategy_id + "_CPP.cpp");
	AGIS_TRY(code_gen_write(header_path, strategy_header))
		AGIS_TRY(code_gen_write(source_path, strategy_source))
}


//============================================================================
AgisResult<bool> AbstractAgisStrategy::validate_market_asset()
{
	if (!this->ev_lambda_struct.has_value()) {
		return AgisResult<bool>(this->get_strategy_id() + " missing abstract lambda strategy");
	}

	ExchangePtr exchange = ev_lambda_struct.value().exchange;
	auto market_asset = exchange->__get_market_asset();
	if (market_asset.is_exception()) return AgisResult<bool>(market_asset.get_exception());
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> AbstractAgisStrategy::set_beta_scale_positions(bool val, bool check)
{
	if (!val) return AgisStrategy::set_beta_scale_positions(val, check);
	if (check) AGIS_DO_OR_RETURN(this->validate_market_asset(), bool);
	return AgisStrategy::set_beta_scale_positions(val);
}


//============================================================================
AgisResult<bool> AbstractAgisStrategy::set_beta_hedge_positions(bool val, bool check)
{
	if (!val) return AgisStrategy::set_beta_hedge_positions(val, check);
	if (check) AGIS_DO_OR_RETURN(this->validate_market_asset(), bool);
	return AgisStrategy::set_beta_hedge_positions(val);
}


//============================================================================
AGIS_API AgisResult<bool> AbstractAgisStrategy::set_beta_trace(bool val, bool check)
{
	if (!val) return AgisStrategy::set_beta_trace(val, check);
	if (check) AGIS_DO_OR_RETURN(this->validate_market_asset(), bool);
	return AgisStrategy::set_beta_trace(val);
}