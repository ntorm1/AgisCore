#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h" 
#include "AgisEnums.h"
#include "AgisRisk.h"


namespace Agis {
	class Asset;
	class AssetTable;
	struct MarketAsset;
	class AssetObserver;
	class TradingCalendar;
}

class Order;
class Exchange;
class AgisRouter;

using namespace Agis;


typedef std::shared_ptr<Exchange> ExchangePtr;
typedef std::shared_ptr<Asset> AssetPtr;
typedef std::unique_ptr<Order> OrderPtr;

class ExchangeMap
{
public:
	AGIS_API ExchangeMap();
	AGIS_API ~ExchangeMap();

	AGIS_API std::expected<bool, AgisException> __build();
	AGIS_API bool step();
	void __clean_up();
	void __clear();

	/// <summary>
	/// restore the exchange map from a serialized state in a json file
	/// </summary>
	/// <param name="j">reference to json object containing information on how to restore the map</param>
	AGIS_API void restore(rapidjson::Document const& j);
	AGIS_API rapidjson::Document to_json() const;

	/// <summary>
	/// Create a new exchange in the exchange map. Allows for grouping of similair assets
	/// </summary>
	/// <param name="exchange_id_">unique id of the exchange</param>
	/// <param name="source_dir_">file path of the folder containing the assets</param>
	/// <param name="freq_">frequency of the exchange data points</param>
	/// <param name="dt_format">the format of the datetime index</param>
	/// <returns>status if the new exchange was created succesfully</returns>
	AGIS_API [[nodiscard]] AgisResult<bool> new_exchange(
		AssetType asset_type,
		std::string exchange_id_,
		std::string source_dir_,
		Frequency freq_,
		std::string dt_format
	);

	AGIS_API [[nodiscard]] AgisResult<bool> restore_exchange(
		std::string const& exchange_id_,
		std::optional<std::vector<std::string>> asset_ids = std::nullopt,
		std::optional<std::shared_ptr<MarketAsset>> market_asset = std::nullopt
	);

	/// <summary>
	/// Remove exchange from the map by exchange id
	/// </summary>
	/// <param name="exchange_id_">id of the exchange to remove</param>
	/// <returns>status of the remove action</returns>
	AGIS_API NexusStatusCode remove_exchange(std::string const& exchange_id_);

	/// <summary>
	/// Get a vector of all asset ids on a particular exchange
	/// </summary>
	/// <param name="exchange_id_">id of the exchange to get</param>
	/// <returns>vector of all asset ids on a particular exchange</returns>
	AGIS_API std::vector<std::string> get_asset_ids(std::string const& exchange_id_) const;

	/// <summary>
	/// Get the current beta of an asset
	/// </summary>
	/// <param name="index">unique index of the asset to look for</param>
	/// <returns>beta if it exists</returns>
	AGIS_API AgisResult<double> get_asset_beta(size_t index) const;


	AGIS_API AgisResult<AssetPtr> get_asset(std::string const& asset_id) const;
	AGIS_API AgisResult<AssetPtr> get_asset(size_t index) const;


	/// <summary>
	/// Remove an asset from the exchange map by asset id
	/// </summary>
	/// <param name="asset_id">unique id of the asset to remove</param>
	/// <returns>the asset if it was removed succsefully</returns>
	AGIS_API AgisResult<AssetPtr> remove_asset(std::string const& asset_id);

	/// <summary>
	/// Get the unique index associated with a asset id
	/// </summary>
	/// <param name="id"></param>
	/// <returns></returns>
	AGIS_API inline size_t get_asset_index(std::string const& id) const { return this->asset_map.at(id); }

	/// <summary>
	/// Get the unique id associated with an asset index
	/// </summary>
	/// <param name="index">index of the asset</param>
	/// <returns>id of the asset</returns>
	AGIS_API AgisResult<std::string> get_asset_id(size_t index) const;

	/// <summary>
	/// Get a shared pointer to an existing exchange
	/// </summary>
	/// <param name="exchange_id">Unique id of the exchange to get</param>
	/// <returns></returns>
	AGIS_API std::expected<ExchangePtr, AgisException> get_exchange(const std::string& exchange_id) const;


	AGIS_API auto const& get_assets() const noexcept { return this->assets; }
	AGIS_API bool asset_exists(std::string const& asset_id) const;

	/// <summary>
	/// Set an asset on the exchange as the market asset, used for beta hedging and benchamrking
	/// </summary>
	/// <param name="asset_id">unique id of the exchange to set</param>
	/// <param name="asset_id">unique id of the market asset</param>
	/// <param name="disable_asset">disable the asset from being used in the exchange view</param>
	/// <param name="beta_lookback">calculate the beta of all assets against the market asset, note adjusts assets warmup</param>
	/// <returns></returns>
	AGIS_API [[nodiscard]] AgisResult<bool> set_market_asset(
		std::string const& exchange_id,
		std::string const& asset_id,
		bool disable_asset,
		std::optional<size_t> beta_lookback
	);

	/**
	 * @brief initialize the covariance matrix
	 * @return result of the attempted initialization
	*/
	AGIS_API AgisResult<bool> init_covariance_matrix(size_t lookback, size_t step_size);

	/**
	 * @brief disable or enable covariance matrix tracking by either adding or removing asset observers
	 * @param enabled
	 * @return
	*/
	AGIS_API AgisResult<bool> set_covariance_matrix_state(bool enabled);

	/**
	 * @brief get a const pointer to the agis covariance matrix
	 * @return
	*/
	AGIS_API AgisResult<std::shared_ptr<AgisCovarianceMatrix>> get_covariance_matrix() const;

	/**
	 * @brief does an exchange with this id exist already
	 * @param id unique id of the exchange to search for
	 * @return does an exchange with this id exist already
	*/
	AGIS_API bool exchange_exists(std::string const& id) const { return this->exchanges.count(id) > 0; };

	/**
	 * @brief Get a vector of all exchange ids
	 * @return vector of all exchange ids
	*/
	AGIS_API std::vector<std::string> get_exchange_ids() const;

	/**
	 * @brief get the total number of rows of asset data currently loaded in
	 * @return total number of rows of asset data currently loaded in
	*/
	AGIS_API size_t get_candle_count() const { return this->candles; }

	AGIS_API size_t get_asset_count() const { return this->assets.size(); }

	/**
	 * @brief get the current datetime of the exchange
	 * @return current datetime of the exchange
	*/
	AGIS_API long long get_datetime() const;

	/**
	 * @brief get the current market price of an asset by its index and wether or not at the close step.
	 * @param asset_index index of the asset to get the price of
	 * @param on_close get the price at the close step
	 * @return current market price
	*/
	AGIS_API double __get_market_price(size_t asset_index, bool on_close) const noexcept;

	/**
	 * @brief get the current market price of an asset by its id and wether or not at the close step.
	 * @param asset_id id of the asset to get the price of
	 * @param on_close get the price at the close step
	 * @return current market price
	*/
	AGIS_API double __get_market_price(std::string& asset_id, bool on_close) const;

	/**
	 * @brief get the market asset for a given frequency if it exists
	 * @param freq frequency to get the market asset for
	 * @return market asset for a given frequency if it exists
	*/
	AGIS_API AgisResult<AssetPtr const> __get_market_asset(Frequency freq) const;

	/**
	 * @brief get the datetime index of the exchange
	 * @param cutoff wether or not to move the start forward by the warmup period
	 * @return datetime index of the exchange
	*/
	AGIS_API std::span<long long> const __get_dt_index(bool cutoff = false) const;

	/**
	 * @brief get the datetime index of the exchange
	 * @return datetime index of the exchange
	*/
	AGIS_API inline long long __get_market_time() const { return this->current_time; }

	/**
	 * @brief get the current index location of the simulation
	 * @return current index location of the simulation
	*/
	AGIS_API inline size_t __get_current_index() const { return this->current_index - 1; }

	/**
	 * @brief run the exchange till a specific datetime
	 * @param datetime datetime to run the exchange till
	 * @return
	*/
	AGIS_API void __goto(long long datetime);

	/**
	 * @brief reset the exchange to the start including all assets listed on the exchange
	 * @return
	*/
	AGIS_API void __reset();

	/// <summary>
	/// Convert a nanosecond epoch timestemp to Timpoint with hour and second for eastern tz
	/// </summary>
	/// <param name="epoch">Epoch to convert</param>
	/// <returns></returns>
	TimePoint epoch_to_tp(long long epoch);
	TimePoint const& get_tp() const { return this->time_point; }

	/**
	 * @brief place an asset in the the assets vector
	 * @param asset_index unique index of the asset
	 * @param asset asset to place in the vector
	*/
	void __set_asset(size_t asset_index, std::shared_ptr<Asset> asset);

	/**
	 * @brief set the volatility lookback period of all exchanges registered
	 * @param window_size lookback period to set
	 * @return
	*/
	AGIS_API void __set_volatility_lookback(size_t window_size);


	/**
	 * @brief place an incoming order on the exchange order queue to be evaluated at the next step
	 * @param order unique pointer to the order to place on the exchange
	*/
	std::optional<bool> __place_order(std::unique_ptr<Order> order) noexcept;

	/**
	 * @brief process all open orders on the exchange and send rejects and fills back to the router
	 * @param router ref to the router to send the rejects and fills to
	 * @param on_close wether or not to process the orders at the close step
	*/
	void __process_orders(AgisRouter& router, bool on_close);

	/**
	 * @brief process an indivual order according to its type
	 * @param on_close wether or not to process the orders at the close step
	 * @param order order to process
	*/
	void __process_order(bool on_close, OrderPtr& order);


	ThreadSafeVector<size_t> const& __get_expired_index_list() const { return this->expired_asset_index; }

private:
	std::mutex _mutex;
	ankerl::unordered_dense::map<std::string, ExchangePtr> exchanges;
	ankerl::unordered_dense::map<std::string, size_t> asset_map;
	ankerl::unordered_dense::map<Frequency, AssetPtr> market_assets;

	std::vector<std::shared_ptr<Asset>> assets;
	std::vector<std::shared_ptr<Asset>> assets_expired;

	ThreadSafeVector<size_t> expired_asset_index;
	std::shared_ptr<AgisCovarianceMatrix> covariance_matrix = nullptr;


	TimePoint time_point;
	long long* dt_index = nullptr;
	long long current_time;

	size_t dt_index_size = 0;
	size_t current_index = 0;
	size_t candles = 0;
	size_t asset_counter = 0;
	bool is_built = false;
};
