#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "pch.h"

#ifdef USE_LUAJIT
#include "AgisLuaStrategy.h"
#endif

#include "AgisPointers.h"
#include "AgisErrors.h"
#include "AgisRouter.h"
#include "AbstractAgisStrategy.h"

#include "Exchange.h"
#include "Portfolio.h"

import Broker;

class Hydra
{
private:
	/// <summary>
	/// Container holding all exchange objects that are currently registered
	/// </summary>
	ExchangeMap exchanges;
	
	/// <summary>
	/// Container holding all portfolios that are currently registered
	/// </summary>
	PortfolioMap portfolios;

	/// <summary>
	/// Container holding all strategies that are currently registered
	/// </summary>
	AgisStrategyMap strategies;
	
	/// <summary>
	/// An AgisRouter to route orders
	/// </summary>
	AgisRouter router;

	/// <summary>
	/// Current index of the backtest
	/// </summary>
	size_t current_index = 0;

	#ifdef USE_LUAJIT
	sol::state* lua = nullptr;
	#endif

	int logging;
	bool is_built = false;

public:
	AGIS_API Hydra(int logging_ = 0, bool init_lua_state = false);
	AGIS_API ~Hydra();

	/// <summary>
	/// Restore hydra instance from a json object
	/// </summary>
	/// <param name="j">Json object describin a Hydra instance</param>
	/// <returns></returns>
	AGIS_API AgisResult<bool> restore_portfolios(json const& j);

	/// <summary>
	/// Restore the underlying data of the hydra instance
	/// </summary>
	/// <param name="j"></param>
	/// <returns></returns>
	AGIS_API AgisResult<bool> restore_exchanges(json const& j);

	/// <summary>
	/// Remove everything from the instance
	/// </summary>
	/// <returns></returns>
	AGIS_API void clear();

	/// <summary>
	/// Build the hydra instance using the currently loaded objects
	/// </summary>
	/// <returns></returns>
	AGIS_API [[nodiscard]] AgisResult<bool> build();
	
	/// <summary>
	/// Reset the hydra instance to it's original state before any steps forward in time
	/// </summary>
	/// <returns></returns>
	AGIS_API void __reset();

	/**
	 * @brief on the completion of a run cleanup nessecary settings
	 * @return whether the cleanup was successful
	*/
	AGIS_API AgisResult<bool> __cleanup();

	/// <summary>
	/// Save the current state of Hydra instance to json
	/// </summary>
	/// <param name="j"></param>
	/// <returns></returns>
	AGIS_API void save_state(json& j);

	/// <summary>
	/// Step Hydra instance one step forward in time
	/// </summary>
	/// <returns></returns>
	AGIS_API void __step();

	/// <summary>
	/// Run a complete Hydra simulation
	/// </summary>
	/// <returns></returns>
	AGIS_API [[nodiscard]] AgisResult<bool> __run();

	AGIS_API void __run_to(long long datetime);


	/// <summary>
	/// Build a new exchange on the hydra instance
	/// </summary>
	/// <param name="exchange_id_">unique id of the exchange</param>
	/// <param name="source_dir_">directory of the files containing the asset data</param>
	/// <param name="freq_">frequency of the asset data</param>
	/// <param name="dt_format">format of the datetime index</param>
	/// <returns></returns>
	AGIS_API [[nodiscard]] AgisResult<bool> new_exchange(
		std::string exchange_id_,
		std::string source_dir_,
		Frequency freq_,
		std::string dt_format,
		std::optional<std::vector<std::string>> asset_ids = std::nullopt,
		std::optional<MarketAsset> market_asset_ = std::nullopt
	);

	/// <summary>
	///	Build a new portfolio on the exchange 
	/// </summary>
	/// <param name="id">unique id of the portfolio</param>
	/// <param name="cash">amount of cash held in the portfolio</param>
	/// <returns></returns>
	AGIS_API PortfolioPtr const new_portfolio(std::string id, double cash);

	/// <summary>
	/// Register new strategy to hydra instance
	/// </summary>
	/// <param name="strategy">Unique pointer to a AgisStrategy</param>
	/// <param name="portfolio_id">portfolio id of the strategy</param>
	/// <returns></returns>
	AGIS_API void register_strategy(AgisStrategyPtr strategy);

	/// <summary>
	/// Get a const ref to the exchange map containing all registered exchanges
	/// </summary>
	/// <returns></returns>
	AGIS_API ExchangeMap const& get_exchanges() const { return this->exchanges; }
	
	/// <summary>
	/// Get const ref to the portfolio map containing all registered portfolios
	/// </summary>
	/// <returns></returns>
	AGIS_API PortfolioMap const& get_portfolios() const { return this->portfolios; }

	/// <summary>
	/// Get ref to the agis strategy map
	/// </summary>
	/// <returns></returns>
	AGIS_API AgisStrategyMap const& __get_strategy_map() const { return this->strategies; }
	
	/// <summary>
	/// Get const ref to a portfolio registered to the hydra instance
	/// </summary>
	/// <param name="portfolio_id">unique id of the portfolio to get</param>
	/// <returns></returns>
	AGIS_API PortfolioPtr const get_portfolio(std::string const& portfolio_id) const;

	/// <summary>
	/// Get pointer to const AgisStrategy registered to the hydra instance
	/// </summary>
	/// <param name="strategy_id">unique id of the strategy</param>
	/// <returns></returns>
	AGIS_API AgisStrategy const* get_strategy(std::string strategy_id) const;

	/// <summary>
	/// Get const ref to a AgisStrategy registered to the hydra instance
	/// </summary>
	/// <param name="strategy_id">unique id of the strategy</param>
	/// <returns></returns>
	AGIS_API AgisStrategy* __get_strategy(std::string strategy_id) const;

	/// <summary>
	/// Get const ref to the portfolio map containing all registered portfolios
	/// </summary>
	/// <returns></returns>
	AGIS_API PortfolioMap& __get_portfolios() { return this->portfolios; }

	/// <summary>
	/// Get a const ref to the vector containing the entire order history
	/// </summary>
	/// <returns></returns>
	AGIS_API ThreadSafeVector<SharedOrderPtr> const& get_order_history() { return this->router.get_order_history(); }

	/// <summary>
	/// Remove exchange from the hydra instance by exchange id
	/// </summary>
	/// <param name="exchange_id_">Unique id of the exchange to remove</param>
	/// <returns></returns>
	AGIS_API NexusStatusCode remove_exchange(std::string exchange_id_);

	/// <summary>
	/// Remove portfolio from the hydra instance by exchange id
	/// </summary>
	/// <param name="portfolio_id_">Unique id of the portfolio to remove</param>
	/// <returns></returns>
	AGIS_API NexusStatusCode remove_portfolio(std::string portfolio_id_);

	/// <summary>
	/// Remove a strategy from the hydra instance
	/// </summary>
	/// <param name="strategy_id">Unique id of the strategy</param>
	/// <returns></returns>
	AGIS_API void remove_strategy(std::string const& strategy_id);
	
	/// <summary>
	/// Get all asset id's currently registered to an exchange
	/// </summary>
	/// <param name="exchange_id_">unique id of the exchange to search</param>
	/// <returns></returns>
	AGIS_API std::vector<std::string> get_asset_ids(std::string exchange_id_) const;
	
	/// <summary>
	/// Get a const ref to an asset registered in the exchange map
	/// </summary>
	/// <param name="asset_id">Unique id of the asset to search for</param>
	/// <returns></returns>
	AGIS_API AgisResult<AssetPtr> get_asset(std::string const& asset_id) const;

	/**
	 * @brief init the covariance matrix for the exchange map
	 * @return result of the operation
	*/
	AGIS_API AgisResult<bool> init_covariance_matrix(size_t lookback, size_t step_size);
	
	AGIS_API [[nodiscard]] AgisResult<bool> set_market_asset(
		std::string const& exchange_id,
		std::string const& asset_id,
		bool disable = true,
		std::optional<size_t> beta_lookback = std::nullopt
	);
	
	AGIS_API inline void __set_strategy_is_live(std::string const& strategy_id, bool is_live) {
		this->__get_strategy(strategy_id)->set_is_live(is_live);
	}

	AGIS_API AgisResult<std::string> asset_index_to_id(size_t const& index) const;
	AGIS_API AgisResult<std::string> strategy_index_to_id(size_t const& index) const;
	AGIS_API AgisResult<std::string> portfolio_index_to_id(size_t const& index) const;

	AGIS_API auto __get_dt_index(bool cutoff = false) const {return this->exchanges.__get_dt_index(cutoff);}
	AGIS_API size_t get_candle_count() { return this->exchanges.get_candle_count(); };
	AGIS_API bool asset_exists(std::string asset_id) const;
	AGIS_API bool portfolio_exists(std::string const& portfolio_id) const;
	AGIS_API bool strategy_exists(std::string const& strategy_id) const;
};