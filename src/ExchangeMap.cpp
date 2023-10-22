#include <execution>

#include "Asset/Asset.h"
#include "Exchange.h"
#include "ExchangeMap.h"

#include "utils_array.h"

using namespace rapidjson;


//============================================================================
void ExchangeMap::restore(rapidjson::Document const& j)
{
	// if exchange does not exist skip
	if (!j.HasMember("exchanges")) return;
	rapidjson::Value const& exchanges = j["exchanges"];
	std::vector<std::pair<std::string, Value const&>> exchangeItems;

	// Store the exchange items in a vector for parallel processing
	for (auto it = exchanges.MemberBegin(); it != exchanges.MemberEnd(); ++it) {
		exchangeItems.emplace_back(it->name.GetString(), it->value);
	}

	this->asset_counter = 0;
	// Process the exchange items 
	std::for_each(std::execution::par, exchangeItems.begin(), exchangeItems.end(), [&](const auto& exchange) {
		auto const& exchange_id_ = exchange.first;
		auto const& exchange_json = exchange.second;
		auto const& source_dir_ = exchange_json["source_dir"].GetString();
		auto const& dt_format_ = exchange_json["dt_format"].GetString();
		auto freq_ = StringToFrequency(exchange_json["freq"].GetString());
		auto asset_type_ = StringToAssetType(exchange_json["asset_type"].GetString());
		this->new_exchange(asset_type_, exchange_id_, source_dir_, freq_, dt_format_);

		// set market asset if needed
		auto market_asset = std::make_shared<MarketAsset>(
			exchange_json["market_asset"].GetString(), exchange_json["market_warmup"].GetInt()
		);

		this->restore_exchange(exchange_id_, std::nullopt, market_asset);


		// set volatility lookback
		auto exchange_ptr = this->get_exchange(exchange_id_).value();
		size_t volatility_lookback = exchange_json["volatility_lookback"].GetUint64();
		exchange_ptr->__set_volatility_lookback(volatility_lookback);
		});

	// set market assets
	for (auto& exchange : this->exchanges)
	{
		auto res = exchange.second->__get_market_asset();
		if (res.is_exception()) continue;
		auto market_asset = res.unwrap();
		this->market_assets[market_asset->get_frequency()] = market_asset;
	}

	// Check if 'covariance_lookback' and 'covariance_step' fields exist
	if (j.HasMember("covariance_lookback") && j.HasMember("covariance_step")) {
		// Extract the values and call the corresponding function
		int covariance_lookback = j["covariance_lookback"].GetInt();
		int covariance_step = j["covariance_step"].GetInt();

		// Call the function with the extracted values
		this->init_covariance_matrix(covariance_lookback, covariance_step);
	}
}


//============================================================================
AgisResult<bool> ExchangeMap::set_market_asset(
	std::string const& exchange_id,
	std::string const& asset_id,
	bool disable_asset,
	std::optional<size_t> beta_lookback)
{
	if (!this->exchange_exists(exchange_id)) return AgisResult<bool>(AGIS_EXCEP("exchange does not exists"));
	if (!this->asset_exists(asset_id)) return AgisResult<bool>(AGIS_EXCEP("asset does not exists"));

	// validate that the a market asset has not already been set for this frequency
	auto asset = this->get_asset(asset_id).unwrap();
	auto freq = asset->get_frequency();
	if (this->market_assets.contains(freq)) return AgisResult<bool>(AGIS_EXCEP("market asset already set for frequency: " + FrequencyToString(freq)));

	// set the market asset to the exchange
	auto res = this->exchanges[exchange_id]->__set_market_asset(asset_id, disable_asset, beta_lookback);
	if (!res.is_exception()) this->is_built = false;

	// store pointer to the market asset
	this->market_assets.emplace(freq, asset);
	return res;
}


//============================================================================
AgisResult<bool> ExchangeMap::init_covariance_matrix(size_t lookback, size_t step_size)
{
	// validate the currently registered exchanges. They all must have the same frequency 
	// and have the same volatility lookback period
	Frequency freq;
	int i = 1;
	for (auto& [id, exchange] : this->exchanges)
	{
		if (i == 1) {
			freq = exchange->get_frequency();
			i++;
		}
		else {
			if (exchange->get_frequency() != freq) {
				return AgisResult<bool>(AGIS_EXCEP("exchange: " + id + " has different frequency"));
			}
		}
	}

	try {
		this->covariance_matrix = std::make_shared<AgisCovarianceMatrix>(this, lookback, step_size);
	}
	catch (std::exception& e) {
		return AgisResult<bool>(AGIS_EXCEP(e.what()));
	}
	return AgisResult<bool>(true);
}


//============================================================================
AGIS_API AgisResult<bool> ExchangeMap::set_covariance_matrix_state(bool enabled)
{
	// no cov matrix exists
	if (!this->covariance_matrix) return AgisResult<bool>(AGIS_EXCEP("covariance matrix not initialized"));
	// disable it by removing all asset observers
	if (!enabled) {
		this->covariance_matrix->clear_observers();
	}
	// enable it by adding all asset observers	
	else {
		this->covariance_matrix->set_asset_observers();
	}

	return AgisResult<bool>(true);
}

//============================================================================
AgisResult<std::shared_ptr<AgisCovarianceMatrix>> ExchangeMap::get_covariance_matrix() const
{
	if (!this->covariance_matrix) return AgisResult<std::shared_ptr<AgisCovarianceMatrix>>(AGIS_EXCEP("covariance matrix not initialized"));
	return AgisResult<std::shared_ptr<AgisCovarianceMatrix>>(this->covariance_matrix);
}


//============================================================================
ExchangeMap::ExchangeMap()
{
}


//============================================================================
ExchangeMap::~ExchangeMap()
{
	if (this->is_built)
	{
		delete[] this->dt_index;
	}
}


//============================================================================
rapidjson::Document ExchangeMap::to_json() const {
	rapidjson::Document j(rapidjson::kObjectType);
	for (const auto& pair : exchanges) {
		rapidjson::Value key(pair.first.c_str(), j.GetAllocator());
		j.AddMember(key.Move(), pair.second->to_json(), j.GetAllocator());
	}
	return j;
}

//============================================================================
AgisResult<bool> ExchangeMap::restore_exchange(
	std::string const& exchange_id_,
	std::optional<std::vector<std::string>> asset_ids,
	std::optional<std::shared_ptr<MarketAsset>> market_asset
)
{
	// Load in the exchange's data
	ExchangePtr exchange = this->exchanges.at(exchange_id_);
	AGIS_DO_OR_RETURN(exchange->restore(asset_ids, market_asset), bool);
	AGIS_DO_OR_RETURN(exchange->validate(), bool);

	// Copy shared pointers to the main asset map
	LOCK_GUARD
		for (const auto& asset : exchange->get_assets())
		{
			// set the unique asset index as the exchange map's counter, then register it
			asset->__set_index(this->asset_counter);
			this->assets.push_back(asset);
			this->asset_map.emplace(asset->get_asset_id(), this->asset_counter);
			this->asset_counter++;
		}
	this->candles += exchange->get_candle_count();

	// set the market asset pointer
	if (market_asset.has_value())
	{
		// find asset in assets with the same id as the market asset
		auto new_market_asset_ptr = std::find_if(
			this->assets.begin(),
			this->assets.end(),
			[&](const AssetPtr& asset) {
				return asset->get_asset_id() == market_asset.value()->market_id;
			}
		);
		if (new_market_asset_ptr == this->assets.end())
		{
			UNLOCK_GUARD
				return AgisResult<bool>(AGIS_EXCEP("Market asset not found"));
		}
		auto market_asset_struct = exchange->__get_market_asset_struct();
		market_asset_struct.value()->asset = *new_market_asset_ptr;
		market_asset_struct.value()->market_index = (*new_market_asset_ptr)->get_asset_index();
	}

	// build asset tables
	auto res = build_asset_tables(exchange.get());
	if (!res.has_value()) AgisResult<bool>(res.error());

	UNLOCK_GUARD
		return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> ExchangeMap::new_exchange(
	AssetType asset_type_,
	std::string exchange_id_,
	std::string source_dir_,
	Frequency freq_,
	std::string dt_format_
)
{
	if (this->exchanges.count(exchange_id_))
	{
		return AgisResult<bool>(AGIS_EXCEP("exchange already exists"));
	}

	// Build the new exchange object and add to the map
	auto exchange = std::make_shared<Exchange>(
		asset_type_,
		exchange_id_,
		source_dir_,
		freq_,
		dt_format_,
		this
	);
	LOCK_GUARD
		this->exchanges.emplace(exchange_id_, exchange);
	UNLOCK_GUARD
		return AgisResult<bool>(true);
}


//============================================================================
std::vector<std::string> ExchangeMap::get_asset_ids(std::string const& exchange_id_) const
{
	if (exchange_id_ != "") {
		return this->exchanges.at(exchange_id_)->get_asset_ids();
	}
	// else get vector of combined asset ids 
	std::vector<std::string> asset_ids;
	for (const auto& pair : this->exchanges) {
		ExchangePtr exchange = pair.second;
		auto ids = exchange->get_asset_ids();
		asset_ids.insert(asset_ids.end(), ids.begin(), ids.end());
	}
	return asset_ids;
}


//============================================================================
std::expected<double, AgisStatusCode> ExchangeMap::get_asset_beta(size_t index) const
{
	auto asset = this->get_asset(index);
	if (asset.is_exception()) return std::unexpected<AgisStatusCode>(AgisStatusCode::INVALID_ARGUMENT);
	return asset.unwrap()->get_beta();
}


//============================================================================
std::expected<double, AgisStatusCode> Exchange::get_asset_beta(size_t index) const
{
	auto asset = this->get_asset(index);
	if (asset.is_exception()) return std::unexpected<AgisStatusCode>(AgisStatusCode::INVALID_ARGUMENT);
	return asset.unwrap()->get_beta();
}


//============================================================================
std::expected<double, AgisStatusCode> Exchange::get_asset_volatility(size_t index) const
{
	auto asset = this->get_asset(index);
	if (asset.is_exception()) return std::unexpected<AgisStatusCode>(AgisStatusCode::INVALID_ARGUMENT);
	return asset.unwrap()->get_volatility();
}


//============================================================================
AgisResult<size_t> Exchange::get_column_index(std::string const& col) const
{
	if (!this->headers.contains(col)) return AgisResult<size_t>(AGIS_EXCEP("missing col: " + col));
	return AgisResult<size_t>(this->headers.at(col));
}


//============================================================================
AgisResult<AssetPtr> ExchangeMap::get_asset(std::string const& asset_id) const
{
	if (!this->asset_exists(asset_id))
	{
		return AgisResult<AssetPtr>(AGIS_EXCEP("asset was not found"));
	}
	auto index = this->asset_map.at(asset_id);
	return AgisResult<AssetPtr>(this->assets[index]);
}


//============================================================================
AgisResult<AssetPtr> ExchangeMap::get_asset(size_t index) const
{
	if (index >= this->assets.size())
	{
		return AgisResult<AssetPtr>(AGIS_EXCEP("asset was not found"));
	}
	return AgisResult<AssetPtr>(this->assets[index]);
}

//============================================================================
AgisResult<AssetPtr> Exchange::__remove_asset(size_t asset_index)
{
	AssetPtr asset = this->assets[asset_index];

	// delete the asset at this index from the assets vector
	this->assets.erase(this->assets.begin() + asset_index);

	return AgisResult<AssetPtr>(asset);
}

//============================================================================
AGIS_API AgisResult<AssetPtr> ExchangeMap::remove_asset(std::string const& asset_id)
{
	if (this->current_index != 0) {
		return AgisResult<AssetPtr>(AGIS_EXCEP("asset can only be removed before run"));
	}
	if (!this->asset_exists(asset_id)) {
		return AgisResult<AssetPtr>(AGIS_EXCEP("asset does not exist"));
	}

	// for all asset's with index greater than the extracted asset, decrement their index
	AgisResult<AssetPtr> asset_res = this->get_asset(asset_id);
	if (asset_res.is_exception()) return AgisResult<AssetPtr>(asset_res.get_exception());
	auto asset = asset_res.unwrap();
	auto asset_index = asset->__get_index();

	// remove from the asset map 
	this->asset_map.erase(asset_id);

	// delete the asset at this index from the assets vector
	this->assets.erase(this->assets.begin() + asset_index);

	// for all assets at this index or more, decrease their index by one
	for (auto& asset : this->assets)
	{
		if (asset->__get_index() >= asset_index)
		{
			asset->__set_index(asset->__get_index() - 1);
			this->asset_map.at(asset->get_asset_id()) = asset->__get_index();
		}
	}

	// remove the asset from the exchange
	ExchangePtr exchange = this->exchanges.at(asset->get_exchange_id());
	AGIS_DO_OR_RETURN(exchange->__remove_asset(asset->__get_index(true)), AssetPtr);

	// decrease the asset counter by 1
	this->asset_counter--;

	// return the extracted asset
	return AgisResult<AssetPtr>(asset);
}


//============================================================================
std::expected<ExchangePtr, AgisException> ExchangeMap::get_exchange(std::string const& exchange_id_) const
{
	auto it = this->exchanges.find(exchange_id_);
	if (it == this->exchanges.end()) {
		return std::unexpected<AgisException>(AGIS_EXCEP("missing exchange: " + exchange_id_));
	}
	return it->second;
}


//============================================================================
std::vector<ExchangePtr> ExchangeMap::get_exchanges() const
{
	std::vector<ExchangePtr> exchanges;
	for (auto& exchange : this->exchanges)
	{
		exchanges.push_back(exchange.second);
	}
	return exchanges;
}


//============================================================================
bool ExchangeMap::asset_exists(std::string const& asset_id) const
{
	if (this->asset_map.count(asset_id))
	{
		return true;
	}
	return false;
}

std::vector<std::string> ExchangeMap::get_exchange_ids() const
{
	std::vector<std::string> keys;
	keys.reserve(this->exchanges.size());

	std::transform(this->exchanges.begin(), this->exchanges.end(), std::back_inserter(keys),
		[](const std::pair<std::string, ExchangePtr>& pair) {
			return pair.first;
		});
	return keys;
}


//============================================================================
AGIS_API long long ExchangeMap::get_datetime() const
{
	if (this->current_index == 0)
	{
		return 0;
	}
	// current index is moved forward on step() so during the step the time is 
	// one index step back from where the current index is
	return this->dt_index[this->current_index - 1];
}


//============================================================================
NexusStatusCode ExchangeMap::remove_exchange(std::string const& exchange_id_)
{
	if (!this->exchanges.count(exchange_id_))
	{
		return NexusStatusCode::InvalidId;
	}
	this->exchanges.erase(exchange_id_);
	return NexusStatusCode::Ok;
}


//============================================================================
std::span<long long> const ExchangeMap::__get_dt_index(bool cutoff) const
{
	if (!this->dt_index) return std::span<long long>();
	if (!cutoff) return std::span(this->dt_index, this->dt_index_size);
	return std::span(this->dt_index, this->current_index - 1);
}


//============================================================================
AgisResult<AssetPtr const>
ExchangeMap::__get_market_asset(Frequency freq) const
{
	if (!this->market_assets.contains(freq)) return AgisResult<AssetPtr const>(AGIS_EXCEP("No market asset found for frequency"));
	return AgisResult<AssetPtr const>(this->market_assets.at(freq));
}


//============================================================================
double
ExchangeMap::__get_market_price(std::string& asset_id, bool on_close) const
{
	auto index = this->asset_map.at(asset_id);
	auto const& asset = this->assets[index];
	if (!asset) return 0.0f;
	if (!asset->__is_streaming) return 0.0f;
	return asset->__get_market_price(on_close);
}


//============================================================================
double
ExchangeMap::__get_market_price(size_t asset_index, bool on_close) const noexcept
{
	auto const& asset = this->assets[asset_index];
	if (!asset) return 0.0f;
	if (!asset->__is_streaming) return 0.0f;
	return asset->__get_market_price(on_close);
}


//============================================================================
AgisResult<std::string> ExchangeMap::get_asset_id(size_t index) const
{
	if (index >= this->assets.size()) return AgisResult<std::string>(AGIS_EXCEP("Index out of range"));
	if (this->assets[index]) {
		return AgisResult<std::string>(this->assets[index]->get_asset_id());
	}
	else {
		return AgisResult<std::string>(this->assets_expired[index]->get_asset_id());

	}
}

//============================================================================
void ExchangeMap::__goto(long long datetime)
{
	while (true) {
		this->step();
		if (this->__get_market_time() >= datetime) {
			break;
		}
	}
}


//============================================================================
void ExchangeMap::__reset()
{
	this->current_index = 0;

	// reset assets that were expired and bring them back in to view
	for (auto& asset : this->assets_expired) {
		if (asset == nullptr) continue;
		this->__set_asset(asset->__get_index(), asset);
	}
	// fill assets_expired with nullptr
	std::fill(assets_expired.begin(), assets_expired.end(), nullptr);

	for (auto& asset : this->assets)
	{
		this->__set_asset(asset->__get_index(), asset);
	}

	// Move exchange time back to 0
	for (auto& exchange : this->exchanges)
	{
		exchange.second->reset();
	}
	this->expired_asset_index.clear();
}


//============================================================================
void ExchangeMap::__set_asset(size_t asset_index, std::shared_ptr<Asset> asset)
{
	LOCK_GUARD
		this->assets[asset_index] = asset;
	UNLOCK_GUARD
}


//============================================================================
AGIS_API void ExchangeMap::__set_volatility_lookback(size_t window_size)
{
	for (auto& [id, exchange] : this->exchanges)
	{
		exchange->__set_volatility_lookback(window_size);
	}
}


//============================================================================
void ExchangeMap::__process_orders(AgisRouter& router, bool on_close)
{
	auto exchange_process = [&](auto& exchange) {
		exchange.second->__process_orders(router, on_close);
		};

	std::for_each(
		this->exchanges.begin(),
		this->exchanges.end(),
		exchange_process
	);
}


//============================================================================
std::optional<bool> ExchangeMap::__place_order(std::unique_ptr<Order> order) noexcept
{
	if (order->get_asset_index() >= this->assets.size()) return std::nullopt;
	auto& asset = this->assets[order->get_asset_index()];
	auto it = this->exchanges.find(asset->get_exchange_id());
	if (it == this->exchanges.end()) return std::nullopt;
	it->second->__place_order(std::move(order));
	return true;
	;
}


//============================================================================
void ExchangeMap::__process_order(bool on_close, OrderPtr& order)
{
	auto& asset = this->assets[order->get_asset_index()];
	auto& exchange = this->exchanges.at(asset->get_exchange_id());
	exchange->__process_order(on_close, order);
}


//============================================================================
inline std::tm localtime_xp(std::time_t& timer)
{
	std::tm bt{};
#if defined(__unix__)
	localtime_r(&timer, &bt);
#elif defined(_MSC_VER)
	localtime_s(&bt, &timer);
#else
	static std::mutex mtx;
	std::lock_guard<std::mutex> lock(mtx);
	bt = *std::localtime(&timer);
#endif
	return bt;
}

//============================================================================
TimePoint ExchangeMap::epoch_to_tp(long long epoch)
{
	// Convert nanosecond epoch to std::chrono::time_point
	epoch /= 1e9;
	time_t epoch_time_as_time_t = epoch;
	struct tm epoch_time = localtime_xp(epoch_time_as_time_t);
	return TimePoint{ epoch_time.tm_hour, epoch_time.tm_min };
}


//============================================================================
AGIS_API std::expected<bool, AgisException> ExchangeMap::__build()
{
	if (this->assets.size() == 0) return true;
	size_t exchange_offset = 0;
	for (auto& exchange_pair : this->exchanges)
	{
		auto res = exchange_pair.second->build(exchange_offset);
		if (!res.has_value()) return res;
		exchange_offset += exchange_pair.second->get_assets().size();
	}

	// build the combined datetime index from all the exchanges
	auto datetime_index_ = container_sorted_union(
		this->exchanges,
		[](const auto& obj)
		{
			return obj->__get_dt_index().get();
		},
		[](const auto& obj)
		{
			return obj->__get_size();
		}
	);

	this->dt_index = get<0>(datetime_index_);
	this->dt_index_size = get<1>(datetime_index_);
	this->is_built = true;
	this->current_time = this->dt_index[0];
	// empty vector to contain expired assets
	this->assets_expired.resize(this->assets.size(), nullptr);
	std::fill(assets_expired.begin(), assets_expired.end(), nullptr);
	return true;
}

void ExchangeMap::__clean_up()
{
	// search through all observers, if no strategy tried to build them then remove
	for (auto& exchange : this->exchanges)
	{
		auto& observers = exchange.second->__get_asset_observers();
		for (auto obv_itr = observers.begin(); obv_itr != observers.end(); )
		{
			auto& observer = *obv_itr;
			if (!observer->get_touch())
			{
				auto asset = observer->get_asset_ptr();
				asset->remove_observer(observer.get());
				obv_itr = observers.erase(obv_itr);
			}
			else
			{
				++obv_itr;
			}
		}
	}
}


//============================================================================
bool ExchangeMap::step()
{
	if (this->current_index == this->dt_index_size)
	{
		return false;
	}

	this->current_time = this->dt_index[this->current_index];

	// get next time
	if (this->current_index + 1 < this->dt_index_size)
	{
		this->next_time = this->dt_index[this->current_index + 1];
	}
	else
	{
		this->next_time = this->dt_index[this->current_index];
	}

	// set the exchagne time point 
	this->time_point = epoch_to_tp(this->current_time);

	expired_asset_index.clear();
	// Define a lambda function that processes each asset
	auto process_exchange = [&](auto& exchange_pair) {
		auto exchange_time = exchange_pair.second->__get_market_time();
		if (exchange_time != this->current_time) { return; }
		exchange_pair.second->step(expired_asset_index);
		exchange_pair.second->__took_step = true;
		};

	std::for_each(
		this->exchanges.begin(),
		this->exchanges.end(),
		process_exchange);

	// remove and expired assets;
	for (auto asset_index : expired_asset_index)
	{
		std::shared_ptr<Asset> expired_asset = this->assets[asset_index];
		this->assets_expired[asset_index] = expired_asset;
		this->__set_asset(asset_index, nullptr);
	}

	this->current_index++;
	return true;
}


//============================================================================
void ExchangeMap::__clear()
{
	this->exchanges.clear();
	this->asset_map.clear();
	this->assets.clear();
	this->assets_expired.clear();
	this->expired_asset_index.clear();
	this->current_index = 0;
	this->candles = 0;
	this->asset_counter = 0;
}

