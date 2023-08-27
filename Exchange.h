#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h" 
#include <string>
#include <utility>

#include "Asset.h"
#include "Order.h"
#include "AgisErrors.h"
#include "AgisPointers.h"
#include "ExchangeView.h"

using json = nlohmann::json;

class Exchange;
class ExchangeMap;
struct ExchangeView;
class AgisRouter;

AGIS_API typedef ankerl::unordered_dense::map<std::string, std::shared_ptr<Exchange>> Exchanges;
AGIS_API typedef std::shared_ptr<Exchange> ExchangePtr;

/// <summary>
/// Struct representing a point in time using eastern time.
/// </summary>
struct TimePoint {
	int hour; 
	int minute;

	bool operator<(TimePoint const& rhs) const {
		if (this->hour < rhs.hour)
			return true;
		else if (this->hour == rhs.hour)
			return this->minute < rhs.minute;
		return false;
	}
	bool operator>(TimePoint const& rhs) const {
		if (this->hour > rhs.hour)
			return true;
		else if (this->hour == rhs.hour)
			return this->minute > rhs.minute;
		return false;
	}
	bool operator==(TimePoint const& rhs) const {
		if (this->hour == rhs.hour && this->minute == rhs.minute)
			return true;
		return false;
	}
};


class  Exchange
{
	friend class ExchangeMap;
public:
	AGIS_API Exchange(
		std::string exchange_id,
		std::string source_dir,
		Frequency freq,
		std::string dt_format,
		ExchangeMap* exchanges
	);
	AGIS_API Exchange() = default;
	AGIS_API ~Exchange();

	/// <summary>
	/// Load in the all asset's found in the exchange's source directory
	/// </summary>
	/// <returns>status if the restore was succesful</returns>
	AGIS_API [[nodiscard]] AgisResult<bool> restore(std::optional<std::vector<std::string>> asset_ids = std::nullopt);

	/// <summary>
	/// Restore data from hdf5 file, assume each dataset is asset, dataset name is asset id
	/// and that 1st column is nanosecond epoch index stored n int64
	/// </summary>
	/// <returns></returns>
	AGIS_API [[nodiscard]] AgisResult<bool> restore_h5(std::optional<std::vector<std::string>> asset_ids = std::nullopt);

	/// <summary>
	/// Serialize the exchange to json format so it can be saved
	/// </summary>
	/// <returns>json object containing exchange's info</returns>
	AGIS_API json to_json() const;

	/// <summary>
	/// Get a vector of ids for all assets listed on the exchange
	/// </summary>
	/// <returns>vector of asset ids</returns>
	AGIS_API std::vector<std::string> get_asset_ids() const;
	
	/// <summary>
	/// Does an asset with this id exist on the exchange
	/// </summary>
	/// <param name="asset_id">unique id to search for</param>
	/// <returns>Does the id already exists</returns>
	AGIS_API bool asset_exists(std::string const& asset_id);
	
	/// <summary>
	/// Get all assets currently registered to the exchange
	/// </summary>
	/// <returns></returns>
	std::vector<std::shared_ptr<Asset>> const& get_assets() const { return this->assets; }

	/// <summary>
	/// Get a view into the exchange by getting a value for each asset visable on the exchange
	/// </summary>
	/// <param name="col">Name of the column to query</param>
	/// <param name="row">Row index to query (0, current) (-1, previous)</param>
	/// <param name="query_type">Type of sorting to do</param>
	/// <param name="N">Number of assets to return, defaults -1 means all</param>
	/// <param name="panic">Panic on an invalid asset request</param>
	/// <returns></returns>
	AGIS_API ExchangeView get_exchange_view(
		std::string const& col,
		int row = 0,
		ExchangeQueryType query_type = ExchangeQueryType::Default,
		int N = -1,
		bool panic = false
	);

	/// <summary>
	/// Get a view into the exchange by applying a function to each asset and getting a double output
	/// </summary>
	/// <param name="func">Function to apply to each asset</param>
	/// <param name="query_type">Type of sorting to do</param>
	/// <param name="N">Number of assets to return</param>
	/// <returns></returns>
	AGIS_API ExchangeView get_exchange_view(
		const std::function<AgisResult<double>(std::shared_ptr<Asset> const&)>& func,
		ExchangeQueryType query_type = ExchangeQueryType::Default,
		int N = -1,
		bool panic = false,
		size_t warmup = 0
	);

	/// <summary>
	/// Remove an asset from an exchange, do not call directly, got through exchange map
	/// </summary>
	/// <param name="asset_id">unique id of the asset to remove</param>
	/// <returns></returns>
	AgisResult<AssetPtr> __remove_asset(size_t asset_index);

	/// <summary>
	/// Get an asset from the exchange by its unique id
	/// </summary>
	/// <param name="index"></param>
	/// <returns></returns>
	AGIS_API AgisResult<AssetPtr> get_asset(size_t index) const;
	AGIS_API AgisResult<double> get_asset_beta(size_t index) const;


	AGIS_API size_t get_candle_count() { return this->candles; };
	AGIS_API inline std::string get_exchange_id() const { return this->exchange_id; }
	AGIS_API inline StridedPointer<long long> const __get_dt_index() const;
	AGIS_API inline size_t const __get_size() const { return this->dt_index_size; }
	AGIS_API inline double __get_market_price(size_t asset_index, bool on_close) const;
	AGIS_API inline long long __get_market_time() { return this->dt_index[this->current_index]; }
	size_t __get_exchange_index() const { return this->current_index - 1; };
	AGIS_API [[nodiscard]] AgisResult<AssetPtr> __get_market_asset();


	void __goto(long long datetime);
	bool __is_valid_order(std::unique_ptr<Order>& order) const;
	void __place_order(std::unique_ptr<Order> order);
	void __process_orders(AgisRouter& router, bool on_close);
	void __process_order(bool on_close, OrderPtr& order);
	void __process_market_order(std::unique_ptr<Order>& order, bool on_close);
	void __process_limit_order(std::unique_ptr<Order>& order, bool on_close);


	void reset();
	void build(size_t exchange_offset);
	bool step(ThreadSafeVector<size_t>& expired_assets);
	bool __took_step = false;

protected:
	/// <summary>
	/// Set an asset on the exchange as the market asset, used for beta hedging and benchamrking
	/// </summary>
	/// <param name="asset_id">unique id of the market asset</param>
	/// <param name="disable_asset">disable the asset from being used in the exchange view</param>
	/// <param name="beta_lookback">calculate the beta of all assets against the market asset, note adjusts assets warmup</param>
	/// <returns></returns>
	AGIS_API [[nodiscard]] AgisResult<bool> __set_market_asset(
		std::string const& asset_id,
		bool disable_asset,
		std::optional<size_t> beta_lookback
	);


private:
	std::mutex _mutex;
	static std::atomic<size_t> exchange_counter;

	std::string exchange_id;
	std::string source_dir;
	std::string dt_format;
	size_t exchange_index;
	Frequency freq;

	std::vector<std::unique_ptr<Order>> orders;
	std::vector<std::unique_ptr<Order>> filled_orders;
	std::vector<std::shared_ptr<Asset>> assets;
	ExchangeMap* exchanges;

	std::optional<MarketAsset> market_asset = std::nullopt;

	long long* dt_index = nullptr;
	long long exchange_time;
	size_t exchange_offset = 0;
	size_t dt_index_size = 0;
	size_t current_index = 0;
	size_t warmup = 0;
	size_t candles = 0;
	bool is_built = false;
};

class ExchangeMap
{
public:
	AGIS_API ExchangeMap();
	AGIS_API ~ExchangeMap();

	AGIS_API void __build();
	AGIS_API bool step();
	AGIS_API void __clear();

	/// <summary>
	/// restore the exchange map from a serialized state in a json file
	/// </summary>
	/// <param name="j">reference to json object containing information on how to restore the map</param>
	AGIS_API void restore(json const& j);
	AGIS_API json to_json() const;

	/// <summary>
	/// Create a new exchange in the exchange map. Allows for grouping of similair assets
	/// </summary>
	/// <param name="exchange_id_">unique id of the exchange</param>
	/// <param name="source_dir_">file path of the folder containing the assets</param>
	/// <param name="freq_">frequency of the exchange data points</param>
	/// <param name="dt_format">the format of the datetime index</param>
	/// <param name="asset_ids">optional vector of asset ids to include in the exchange</param>
	/// <returns>status if the new exchange was created succesfully</returns>
	AGIS_API [[nodiscard]] AgisResult<bool> new_exchange(
		std::string exchange_id_,
		std::string source_dir_,
		Frequency freq_,
		std::string dt_format,
		std::optional<std::vector<std::string>> asset_ids = std::nullopt
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

	/// <summary>
	/// Get an asset by it's unique asset id 
	/// </summary>
	/// <param name="asset_id">id of the asset to search for</param>
	/// <returns>shared pointer to asset if it is found</returns>
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
	AGIS_API ExchangePtr const get_exchange(const std::string& exchange_id) const;
	
	/// <summary>
	/// Does a asset with this id exist already
	/// </summary>
	/// <param name="asset_id">id of the asset to search for</param>
	/// <returns>Does a asset with this id exist already</returns>
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

	AGIS_API bool exchange_exists(std::string const& id) const { return this->exchanges.count(id) > 0; };
	AGIS_API std::vector<std::string> get_exchange_ids() const;
	AGIS_API size_t get_candle_count() const { return this->candles; }
	AGIS_API long long get_datetime() const;
	AGIS_API double __get_market_price(size_t asset_index, bool on_close) const;
	AGIS_API double __get_market_price(std::string& asset_id, bool on_close) const;
	
	AGIS_API std::span<long long> const __get_dt_index() const;
	AGIS_API inline long long __get_market_time() { return this->dt_index[this->current_index]; }

	AGIS_API void __goto(long long datetime);
	AGIS_API void __reset();
	
	/// <summary>
	/// Convert a nanosecond epoch timestemp to Timpoint with hour and second for eastern tz
	/// </summary>
	/// <param name="epoch">Epoch to convert</param>
	/// <returns></returns>
	TimePoint epoch_to_tp(long long epoch);
	TimePoint const& get_tp() const {return this->time_point;}
	
	void __set_asset(size_t asset_index, std::shared_ptr<Asset> asset);

	void __place_order(std::unique_ptr<Order> order);
	void __process_orders(AgisRouter& router, bool on_close);
	void __process_order(bool on_close, OrderPtr& order);


	ThreadSafeVector<size_t> const& __get_expired_index_list() const { return this->expired_asset_index; }

private:
	std::mutex _mutex;
	ankerl::unordered_dense::map<std::string, ExchangePtr> exchanges;
	ankerl::unordered_dense::map<std::string, size_t> asset_map;
	std::vector<std::shared_ptr<Asset>> assets;
	std::vector<std::shared_ptr<Asset>> assets_expired;
	ThreadSafeVector<size_t> expired_asset_index;


	TimePoint time_point;
	long long* dt_index = nullptr;
	size_t dt_index_size = 0;
	size_t current_index = 0;
	size_t candles = 0;
	size_t asset_counter = 0;
	bool is_built = false;
};
