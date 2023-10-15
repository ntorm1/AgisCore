#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h" 
#include <string>
#include <utility>

#include "Order.h"
#include "AgisRisk.h"
#include "AgisEnums.h"
#include "ExchangeView.h"

namespace Agis {
	class Asset;
	class AssetTable;
	struct MarketAsset;
	class AssetObserver;
	class TradingCalendar;
}

using namespace Agis;


class Exchange;
class ExchangeMap;
struct ExchangeView;
class AgisRouter;

AGIS_API typedef ankerl::unordered_dense::map<std::string, std::shared_ptr<Exchange>> Exchanges;
AGIS_API typedef std::shared_ptr<Exchange> ExchangePtr;
typedef  std::shared_ptr<AssetObserver> AssetObserverPtr;
typedef std::shared_ptr<Asset> AssetPtr;
typedef std::shared_ptr<AssetTable> AssetTablePtr;

class  Exchange
{
	friend class AssetTable;
	friend class ExchangeMap;
public:
	AGIS_API Exchange(
		AssetType asset_type,
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
	/// <param name="asset_ids">optional vector of asset ids to load</param>
	/// <param name="market_asset">optional market asset to load</param>
	/// <returns>status if the load was succesful</returns>
	AGIS_API [[nodiscard]] AgisResult<bool> restore(
		std::optional<std::vector<std::string>> asset_ids = std::nullopt,
		std::optional<std::shared_ptr<MarketAsset>> market_asset = std::nullopt
	);

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
	AGIS_API rapidjson::Document to_json() const;

	/// <summary>
	/// Get a vector of ids for all assets listed on the exchange
	/// </summary>
	/// <returns>vector of asset ids</returns>
	AGIS_API std::vector<std::string> get_asset_ids() const;
	AGIS_API std::vector<size_t> get_asset_indices() const;

	AGIS_API bool asset_exists(std::string const& asset_id);
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
		const std::function<std::expected<double,AgisErrorCode>(std::shared_ptr<Asset> const&)>& func,
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

	/**
	 * @brief Get an asset table from the exchange 
	 * @tparam T Type of asset table to cast too
	 * @param table_name name of the table to get
	 * @return asset table if it exists
	*/
	template <typename T>
	typename std::enable_if<std::is_base_of<AssetTable, T>::value, std::expected<std::shared_ptr<T>, AgisException>>::type
	get_asset_table(std::string const& table_name) const noexcept
	{
		auto it = this->asset_tables.find(table_name);
		if (it == this->asset_tables.end()) {
			return std::unexpected<AgisException>(AGIS_EXCEP("table does not exist"));
		}
		auto& table = it->second;
		auto derived_table = std::dynamic_pointer_cast<T>(table);
		if (!derived_table) {
			return std::unexpected<AgisException>(AGIS_EXCEP("table is not of type"));
		}
		return derived_table;
	}

	AGIS_API AgisResult<AssetPtr> get_asset(size_t index) const;
	AGIS_API AgisResult<double> get_asset_beta(size_t index) const;
	AGIS_API AgisResult<size_t> get_column_index(std::string const& col) const;

	AGIS_API [[nodiscard]] AssetType get_asset_type() const noexcept { return this->asset_type; }
	AGIS_API size_t get_candle_count() const noexcept { return this->candles; };
	AGIS_API size_t get_asset_count() const noexcept { return this->assets.size(); }
	AGIS_API inline auto get_frequency() const { return this->freq; }
	AGIS_API inline std::string get_source() const noexcept { return this->source_dir; }
	AGIS_API inline std::string get_dt_format() const noexcept { return this->dt_format; }
	AGIS_API inline std::string get_exchange_id() const noexcept { return this->exchange_id; }
	AGIS_API inline StridedPointer<long long> const __get_dt_index() const;
	AGIS_API inline size_t const __get_size() const { return this->dt_index_size; }
	AGIS_API inline double __get_market_price(size_t asset_index, bool on_close) const;
	AGIS_API inline long long __get_market_time() { return this->dt_index[this->current_index]; }
	AGIS_API inline size_t __get_vol_lookback() const { return this->volatility_lookback; }
	size_t __get_exchange_index() const { return this->current_index - 1; };
	AGIS_API [[nodiscard]] AgisResult<AssetPtr> __get_market_asset() const;
	AGIS_API [[nodiscard]] auto const& __get_assets() const { return this->assets; };
	AGIS_API [[nodiscard]] ExchangeMap const* __get_exchange_map() const { return this->exchanges; };
	AGIS_API [[nodiscard]] std::optional<std::shared_ptr<MarketAsset>> __get_market_asset_struct() const noexcept;
	AGIS_API [[nodiscard]] size_t __get_exchange_offset() const { return this->exchange_offset; };
	AGIS_API [[nodiscard]] auto& __get_asset_observers() { return this->asset_observers; };

	void __goto(long long datetime);
	bool __is_valid_order(std::unique_ptr<Order>& order) const;
	void __place_order(std::unique_ptr<Order> order) noexcept;
	void __process_orders(AgisRouter& router, bool on_close);
	void __process_order(bool on_close, OrderPtr& order);
	void __process_market_order(std::unique_ptr<Order>& order, bool on_close);
	void __process_limit_order(std::unique_ptr<Order>& order, bool on_close);
	AGIS_API void __set_volatility_lookback(size_t window_size);
	void __add_asset_observer(AssetObserverPtr&& observer) {this->asset_observers.push_back(std::move(observer));}
	void __add_asset_observer(AssetObserverPtr observer) { this->asset_observers.push_back(std::move(observer)); }
	void __add_asset_table(AssetTablePtr&& table) noexcept;

	AGIS_API std::expected<bool, AgisException> load_trading_calendar(std::string const& path);
	std::shared_ptr<TradingCalendar> get_trading_calendar() const noexcept {return this->_calendar; }

	AgisResult<bool> validate();
	void reset();
	std::expected<bool, AgisException> build(size_t exchange_offset);
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
	AssetType asset_type;
	std::string exchange_id;
	std::string source_dir;
	std::string dt_format;
	size_t exchange_index;
	Frequency freq;

	std::vector<std::unique_ptr<Order>> orders;
	std::vector<std::unique_ptr<Order>> filled_orders;
	std::vector<AssetPtr> assets;
	ankerl::unordered_dense::map<std::string, std::shared_ptr<Agis::AssetTable>> asset_tables;
	std::vector<std::shared_ptr<AssetObserver>> asset_observers;

	ankerl::unordered_dense::map<std::string, size_t> headers;
	ExchangeMap* exchanges;

	std::shared_ptr<TradingCalendar> _calendar = nullptr;
	std::optional<std::shared_ptr<MarketAsset>> market_asset = std::nullopt;

	long long* dt_index = nullptr;
	long long exchange_time;
	size_t exchange_offset = 0;
	size_t dt_index_size = 0;
	size_t current_index = 0;
	size_t warmup = 0;
	size_t volatility_lookback = 0;
	size_t candles = 0;
	bool is_built = false;
};


//============================================================================
template <typename Func, typename... Args>
AgisResult<bool> exchange_add_observer(
	ExchangePtr exchange,
	Func func, 
	Args&&... args
) {
	auto& observers = exchange->__get_asset_observers();
	for (auto& asset : exchange->get_assets()) {
		std::expected<AssetObserverPtr, AgisException> observer = func(asset.get(), std::forward<Args>(args)...);
		if (!observer.has_value()) return AgisResult<bool>(observer.error());
		exchange->__add_asset_observer(observer.value());
		auto asset_obv_raw = observers.back().get();
		asset->add_observer(asset_obv_raw);
	}
	return AgisResult<bool>(true);
}