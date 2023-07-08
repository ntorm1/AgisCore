#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include <optional>

#include "json.hpp"


#include "AgisPointers.h"
#include "AgisErrors.h"
#include "AgisRouter.h"
#include "AgisStrategy.h"

#include "Exchange.h"
#include "Portfolio.h"

using json = nlohmann::json;

#ifdef __cplusplus
extern "C" {
#endif

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

	int logging;

public:
	AGIS_API Hydra(int logging_ = 0);
	AGIS_API ~Hydra() = default;

	AGIS_API void restore(json const& j);
	AGIS_API void clear();
	AGIS_API void build();
	AGIS_API void reset();
	AGIS_API void save_state(json& j);

	AGIS_API void __step();


	/// <summary>
	/// Build a new exchange on the hydra instance
	/// </summary>
	/// <param name="exchange_id_">unique id of the exchange</param>
	/// <param name="source_dir_">directory of the files containing the asset data</param>
	/// <param name="freq_">frequency of the asset data</param>
	/// <param name="dt_format">format of the datetime index</param>
	/// <returns></returns>
	AGIS_API NexusStatusCode new_exchange(
		std::string exchange_id_,
		std::string source_dir_,
		Frequency freq_,
		std::string dt_format
	);

	/// <summary>
	///	Build a new portfolio on the exchange 
	/// </summary>
	/// <param name="id">unique id of the portfolio</param>
	/// <param name="cash">amount of cash held in the portfolio</param>
	/// <returns></returns>
	AGIS_API void new_portfolio(std::string id, double cash);

	/// <summary>
	/// Register new strategy to hydra instance
	/// </summary>
	/// <param name="strategy">Unique pointer to a AgisStrategy</param>
	/// <param name="portfolio_id">portfolio id of the strategy</param>
	/// <returns></returns>
	AGIS_API void register_strategy(std::unique_ptr<AgisStrategy> strategy);

	/// <summary>
	/// Get a const ref to the exchange map containing all registered exchanges
	/// </summary>
	/// <returns></returns>
	AGIS_API ExchangeMap const& get_exchanges() { return this->exchanges; }
	
	/// <summary>
	/// Get const ref to the portfolio map containing all registered portfolios
	/// </summary>
	/// <returns></returns>
	AGIS_API PortfolioMap const& get_portfolios() { return this->portfolios; }
	
	/// <summary>
	/// Get const ref to a portfolio registered to the hydra instance
	/// </summary>
	/// <param name="portfolio_id">unique id of the portfolio to get</param>
	/// <returns></returns>
	AGIS_API PortfolioPtr const& get_portfolio(std::string const& portfolio_id);

	AGIS_API std::vector<OrderPtr> const& get_order_history() { return this->router.get_order_history(); }

	/// <summary>
	/// Remove exchange from the hydra instance by exchange id
	/// </summary>
	/// <param name="exchange_id_">Unique id of the exchange to remove</param>
	/// <returns></returns>
	AGIS_API NexusStatusCode remove_exchange(std::string exchange_id_);
	
	/// <summary>
	/// Get all asset id's currently registered to an exchange
	/// </summary>
	/// <param name="exchange_id_">unique id of the exchange to search</param>
	/// <returns></returns>
	AGIS_API std::vector<std::string> get_asset_ids(std::string exchange_id_);
	
	/// <summary>
	/// Get a const ref to an asset registered in the exchange map
	/// </summary>
	/// <param name="asset_id">Unique id of the asset to search for</param>
	/// <returns></returns>
	AGIS_API std::optional<std::shared_ptr<Asset> const> get_asset(std::string const& asset_id) const;
	
	AGIS_API bool asset_exists(std::string asset_id) const;
};

#ifdef __cplusplus
}
#endif