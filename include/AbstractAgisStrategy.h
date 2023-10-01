#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"
#include "AgisStrategy.h"


class AbstractAgisStrategy : public AgisStrategy {
public:
	using AbstractExchangeViewLambda = std::function<
		std::optional<ExchangeViewLambdaStruct>
		()>;

	AbstractAgisStrategy(
		PortfolioPtr const& portfolio_,
		std::string const& strategy_id,
		double allocation
	) : AgisStrategy(strategy_id, portfolio_, allocation) {
		this->strategy_type = AgisStrategyType::FLOW;
	}

	AGIS_API void next() override;

	AGIS_API inline void reset() override {}

	AGIS_API void build() override;

	/**
	 * @brief extract the information from a node editor graph and build the abstract strategy.
	 * @return wether or not the strategy was built successfully
	*/
	AGIS_API [[nodiscard]] AgisResult<bool> extract_ev_lambda();

	/**
	 * @brief set the function call that will be used to extract the information from a node editor graph and build the abstract strategy.
	 * @param f_ function call that will be used to extract the information from a node editor graph and build the abstract strategy.
	 * @return
	*/
	AGIS_API inline void set_abstract_ev_lambda(std::function<
		std::optional<ExchangeViewLambdaStruct>
		()> f_) {
		this->ev_lambda = f_;
	};

	AGIS_API void restore(fs::path path) override;

	/**
	 * @brief output code to a file that can be used to build a concrete strategy from the abstract strategy
	 * @param strat_folder the destiniation of the output source code.
	 * @return
	*/
	AGIS_API void code_gen(fs::path strat_folder);

	AGIS_API [[nodiscard]] AgisResult<bool> set_beta_trace(bool val, bool check = true);
	AGIS_API [[nodiscard]] AgisResult<bool> set_beta_scale_positions(bool val, bool check = true) override;
	AGIS_API [[nodiscard]] AgisResult<bool> set_beta_hedge_positions(bool val, bool check = true) override;
	AgisResult<bool> validate_market_asset();

private:
	AbstractExchangeViewLambda ev_lambda;
	std::optional<ExchangeViewLambdaStruct> ev_lambda_struct = std::nullopt;
	std::optional<double> ev_opp_param = std::nullopt;

	ExchangeViewOpp ev_opp_type = ExchangeViewOpp::UNIFORM;

	/// <summary>
	/// The number if steps need to happen on the target exchange before the 
	/// strategy next() method is called
	/// </summary>
	size_t warmup = 0;
};

