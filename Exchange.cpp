#include "pch.h" 
#include <execution>
#include <tbb/parallel_for_each.h>
#include <chrono>
#include <Windows.h>
#include "utils_array.h"
#include "Exchange.h"

#include "AgisRouter.h"
#include "AgisRisk.h"

std::atomic<size_t> Exchange::exchange_counter(0);
std::vector<std::string> exchange_view_opps = {
	"UNIFORM", "LINEAR_DECREASE", "LINEAR_INCREASE","CONDITIONAL_SPLIT","UNIFORM_SPLIT",
	"CONSTANT"
};


//============================================================================
Exchange::Exchange(
		std::string exchange_id_, 
		std::string source_dir_,
		Frequency freq_,
		std::string dt_format_,
		ExchangeMap* exchanges_) : 
	exchanges(exchanges_)
{
	this->exchange_id = exchange_id_;
	this->source_dir = source_dir_;
	this->freq = freq_;
	this->dt_format = dt_format_;
	this->exchange_index = exchange_counter++;
}


//============================================================================
Exchange::~Exchange()
{
	if (this->is_built)
	{
		delete[] this->dt_index;
	}
}


//============================================================================
void ExchangeMap::restore(json const& j)
{
	json exchanges = j["exchanges"];
	std::vector<std::pair<std::string, json>> exchangeItems;

	// Store the exchange items in a vector for parallel processing
	for (const auto& exchange : exchanges.items())
	{
		exchangeItems.emplace_back(exchange.key(), exchange.value());
	}

	this->asset_counter = 0;
	// Process the exchange items 
	std::for_each(std::execution::par, exchangeItems.begin(), exchangeItems.end(), [&](const auto& exchange) {
		auto const& exchange_id_ = exchange.first;
		auto const& exchange_json = exchange.second;
		auto const& source_dir_ = exchange_json["source_dir"];
		auto const& dt_format_ = exchange_json["dt_format"];
		auto freq_ = string_to_freq(exchange_json["freq"]);
		this->new_exchange(exchange_id_, source_dir_, freq_, dt_format_);

		// set market asset if needed
		MarketAsset market_asset = MarketAsset(
			exchange_json["market_asset"], exchange_json["market_warmup"]
		);

		this->restore_exchange(exchange_id_, std::nullopt, market_asset);

		// set volatility lookback
		auto exchange_ptr = this->get_exchange(exchange_id_);
		size_t volatility_lookback = exchange_json.value("volatility_lookback", 0);
		exchange_ptr->__set_volatility_lookback(volatility_lookback);
	});

	// set market assets
	for (auto& exchange : this->exchanges)
	{
		auto res = exchange.second->__get_market_asset();
		if(res.is_exception()) continue;
		auto market_asset = res.unwrap();
		this->market_assets[market_asset->get_frequency()] = market_asset;
	}

	// init covariance matrix if needed
	if (j.contains("covariance_lookback")) {
		this->init_covariance_matrix(j["covariance_lookback"], j["covariance_step"]);
	}

}


//============================================================================
json Exchange::to_json() const
{
	auto j = json{
		{"exchange_id", this->exchange_id},
		{"source_dir", this->source_dir},
		{"freq", this->freq},
		{"dt_format", this->dt_format},
		{"volatility_lookback", this->volatility_lookback}
	};
	if (this->market_asset.has_value())
	{
		j["market_asset"] = this->market_asset.value().asset->get_asset_id();
		j["market_warmup"] = this->market_asset.value().beta_lookback.value();
	}
	else
	{
		j["market_asset"] = "";
		j["market_warmup"] = 0;
	}
	return j;
}



//============================================================================
std::vector<std::string> Exchange::get_asset_ids() const
{
	std::vector<std::string> keys;
	keys.reserve(this->assets.size());  // Reserve space in the vector for efficiency

	// Iterate over the map and extract the keys
	for (const auto& asset : this->assets) {
		keys.push_back(asset->get_asset_id());
	}
	return keys;
}


//============================================================================
AGIS_API ExchangeView Exchange::get_exchange_view(
	std::string const& col,
	int row,
	ExchangeQueryType query_type,
	int N,
	bool panic
)
{
	if (row > 0) { throw std::runtime_error("Row must be <= 0"); }
	auto number_assets = (N == -1) ? this->assets.size() : static_cast<size_t>(N);

	ExchangeView exchange_view(this, number_assets);
	auto& view = exchange_view.view;

	for (auto& asset : this->assets)
	{
		if (!asset || !asset->__in_exchange_view) continue;
		if (!asset->__is_streaming)
		{
			if (panic) throw std::runtime_error("invalid asset found"); 
			continue;
		}
		auto val = asset->get_asset_feature(col, row);
		if (val.is_exception() && !panic)
		{
			continue;
		}
		auto v = val.unwrap();
		view.emplace_back( asset->get_asset_index(), v );
	}
	if (view.size() == 1) { return exchange_view; }
	exchange_view.sort(number_assets, query_type);
	return exchange_view;
}


//============================================================================
AGIS_API ExchangeView Exchange::get_exchange_view(
	const std::function<AgisResult<double>(std::shared_ptr<Asset>const&)>& func,
	ExchangeQueryType query_type, 
	int N,
	bool panic,
	size_t warmup)
{
	auto number_assets = (N == -1) ? this->assets.size() : static_cast<size_t>(N);
	ExchangeView exchange_view(this, number_assets);
	auto& view = exchange_view.view;
	AgisResult<double> val;
	for (auto const& asset : this->assets)
	{
		if (!asset || !asset->__in_exchange_view) continue;	// asset not in view, or disabled
		if (!asset->__is_streaming) continue;				// asset is not streaming
		val = func(asset);
		if (val.is_exception()) {
			if (panic) throw val.get_exception();
			else continue;
		}
		auto x = val.unwrap();			
		// check if x is nan (asset filter operations will cause this)
		if(std::isnan(x)) continue;
		view.emplace_back(asset->get_asset_index(), x);
	}

	exchange_view.sort(number_assets, query_type);
	return exchange_view;
}




//============================================================================
AgisResult<bool> Exchange::__set_market_asset(
	std::string const& asset_id,
	bool disable,
	std::optional<size_t> beta_lookback)
{
	if(!this->asset_exists(asset_id)) return AgisResult<bool>(AGIS_EXCEP("asset does not exists"));

	// search in the assets vector for asset with asset_id
	AssetPtr market_asset_ = nullptr;
	for (auto& asset_mid : this->assets)
	{
		if (asset_mid->get_asset_id() == asset_id)
		{
			market_asset_ = asset_mid;
			break;
		}
	}
	
	// check to see if asset encloses all assets listed on the exchange
	for (AssetPtr asset_mid : this->assets)
	{
		auto result = market_asset_->encloses(asset_mid);
		if (result.is_exception() || !result.unwrap())
		{
			return AgisResult<bool>(AGIS_EXCEP("asset does not enclose: " + asset_mid->get_asset_id()));
		}
	}

	// set the market asset and disable it from the exchange view
	market_asset_->__in_exchange_view = false;
	this->market_asset = { market_asset_ , beta_lookback};

	if(!beta_lookback.has_value()) return AgisResult<bool>(true);

	// load the beta columns in for each asset
	std::for_each(this->assets.begin(), this->assets.end(), [&](const auto& asset_mid) {
		// calculate rolling beta column
		if (asset_mid->get_asset_id() != asset_id) {
			asset_mid->__set_beta(market_asset_, beta_lookback.value());
		}
		// adjust the lookback of the market asset to line up with the others
		else {
			asset_mid->__is_market_asset = true;
			asset_mid->__set_warmup(beta_lookback.value());
		}
		});
	
	// once market asset has been added rebuild the exchange to account for the new
	// asset warmup period needed
	this->is_built = false;

	return AgisResult<bool>(true);
}


//============================================================================
AGIS_API [[nodiscard]] AgisResult<AssetPtr> Exchange::__get_market_asset() const
{
	if(!this->market_asset.has_value()) return AgisResult<AssetPtr>(AGIS_EXCEP("market asset not set"));
	return AgisResult<AssetPtr>(this->market_asset.value().asset);
}


//============================================================================
AgisResult<bool> ExchangeMap::set_market_asset(
	std::string const& exchange_id,
	std::string const& asset_id,
	bool disable_asset,
	std::optional<size_t> beta_lookback)
{
	if(!this->exchange_exists(exchange_id)) return AgisResult<bool>(AGIS_EXCEP("exchange does not exists"));
	if(!this->asset_exists(asset_id)) return AgisResult<bool>(AGIS_EXCEP("asset does not exists"));

	// validate that the a market asset has not already been set for this frequency
	auto asset = this->get_asset(asset_id).unwrap();
	auto freq = asset->get_frequency();
	if(this->market_assets.contains(freq)) return AgisResult<bool>(AGIS_EXCEP("market asset already set for frequency: " + freq_to_string(freq)));
	
	// set the market asset to the exchange
	auto res = this->exchanges[exchange_id]->__set_market_asset(asset_id, disable_asset, beta_lookback);
	if (!res.is_exception()) this->is_built = false;

	// store pointer to the market asset
	this->market_assets.emplace(freq, asset);
	return res;
}


//============================================================================
AGIS_API AgisResult<bool> ExchangeMap::init_covariance_matrix(size_t lookback, size_t step_size)
{
	// validate the currently registered exchanges. They all must have the same frequency 
	// and have the same volatility lookback period
	Frequency freq;
	int i = 1;
	for (auto& [id,exchange] : this->exchanges)
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
AGIS_API AgisResult<std::shared_ptr<AgisCovarianceMatrix>> ExchangeMap::get_covariance_matrix() const
{
	if(!this->covariance_matrix) return AgisResult<std::shared_ptr<AgisCovarianceMatrix>>(AGIS_EXCEP("covariance matrix not initialized"));
	return AgisResult<std::shared_ptr<AgisCovarianceMatrix>>(this->covariance_matrix);
}


//============================================================================
AgisResult<AssetPtr> Exchange::get_asset(size_t index) const
{
	auto scaled_index = index - this->exchange_offset;
	if (scaled_index >= this->assets.size()) return AgisResult<AssetPtr>(AGIS_EXCEP("index out of range"));
	return AgisResult<AssetPtr>(this->assets[scaled_index]);
}


//============================================================================
StridedPointer<long long> const Exchange::__get_dt_index() const
{
	return StridedPointer(this->dt_index, this->dt_index_size, 1);
}


//============================================================================
bool Exchange::asset_exists(std::string const& asset_id)
{
	for (const auto& asset : this->assets)
	{
		if (asset->get_asset_id() == asset_id)
		{
			return true;
		}
	}
	return false;
}


//============================================================================
void Exchange::__goto(long long datetime)
{
	// goto date is beyond the datetime index
	if (datetime >= this->dt_index[this->dt_index_size - 1])
	{
		this->current_index = this->dt_index_size;
		return;
	}

	// goto date is before the first date in the index
	else if (datetime <= this->dt_index[0])
	{
		this->current_index = 0;
		return;
	}

	// search for datetime in the index
	for (size_t i = this->current_index; i < this->dt_index_size; i++)
	{
		//is >= right?
		if (this->dt_index[i] >= datetime)
		{
			this->current_index = i;
			return;
		}
	}
}


AgisResult<bool> Exchange::validate()
{
	// loop over all assets in the exchange and make sure the have all have the same
	// column mapping
	bool _first = true;
	for (auto& asset : this->assets)
	{
		if (_first)
		{
			this->headers = asset->get_headers();
			_first = false;
		}
		auto& other_headers = asset->get_headers();
		for(auto& header : this->headers)
		{
			if (other_headers.find(header.first) == other_headers.end())
			{
				return AgisResult<bool>(AGIS_EXCEP("asset headers do not match"));
			}
		}
	}
	return AgisResult<bool>(true);
}

//============================================================================
void Exchange::reset()
{
	this->current_index = 0;
}


//============================================================================
void Exchange::build(size_t exchange_offset_)
{
	if (this->is_built)
	{
		delete[] this->dt_index;
	}

	// Generate date time index as sorted union of each asset's datetime index
	auto datetime_index_ = vector_sorted_union(
		this->assets,
		[](std::shared_ptr<Asset> const obj) -> long long*
		{ 
			if (obj->get_rows() < obj->get_warmup()) return nullptr; // exclude invlaid assets
			return obj->__get_dt_index(true).data();
		},
		[](std::shared_ptr<Asset> const obj)
		{ 
			return obj->get_size();
		});
	this->dt_index = get<0>(datetime_index_);
	this->dt_index_size = get<1>(datetime_index_);
	this->candles = 0;

	for (auto& asset : this->assets) {
		// test to see if asset is alligned with the exchage's datetime index
		// makes updating market view faster
		asset->__reset();
		if (asset->get_rows() == this->dt_index_size) 
		{
			asset->__set_alignment(true);
			asset->__is_streaming = true;
		}
		else
		{
			asset->__set_alignment(false);
		}


		//set the asset's exchange offset so indexing into the exchange's asset vector works
		asset->__set_exchange_offset(exchange_offset_);

		this->candles += asset->get_rows();
	}
	this->exchange_offset = exchange_offset_;
	this->is_built = true;
}


//============================================================================
bool Exchange::step(ThreadSafeVector<size_t>& expired_assets)
{
	// if the current index is the last then return false, all assets listed on this exchange
	// are done streaming their data
	if (this->current_index == this->dt_index_size)
	{
		return false;
	}

	// set exchange time to compare to assets
	this->exchange_time = this->dt_index[this->current_index];
	 
	// Define a lambda function that processes each asset
	auto process_asset = [&](auto& asset) {
		// if asset is expired skip
		if (!asset || asset->__is_expired)
		{
			return;
		}

		// if asset is alligned to exchange just step forward in time, clean up if needed 
		if (asset->__is_aligned) {
			asset->__step();
			return;
		}

		// test to see if this is the last row of data for the asset
		if (asset->__is_last_view()) {
			auto index = asset->__get_index(true);
			expired_assets.push_back(index);
			asset->__is_expired = true;
			this->assets[index] = nullptr;
			return;
		}

		// get the asset's current time
		if (asset->__get_asset_time() == this->exchange_time)
		{
			// add asset to market view, step the asset forward in time. Note if the asset
			// is in warmup it will step forward but __is_streaming is false
			asset->__step();
		}
		else
		{
			asset->__is_streaming = false;
		}

		// check to see if the asset's next time step is the same as the exchagnes
		if (this->current_index < this->dt_index_size)
		{
			if (asset->__get_asset_time() !=
				this->dt_index[this->current_index + 1])
			{
				asset->__is_valid_next_time = false;
			}
			else asset->__is_valid_next_time = true;
		}
	};

	tbb::parallel_for_each(
		this->assets.begin(),
		this->assets.end(),
		process_asset);

	// move to next datetime and return true showing the market contains at least one
	// asset that is not done streaming
	this->current_index++;
	return true;
}



AGIS_API AgisResult<bool> Exchange::restore_h5(std::optional<std::vector<std::string>> asset_ids)
{
	H5::H5File file(this->source_dir, H5F_ACC_RDONLY);
	int numObjects = file.getNumObjs();

	// Read data from each dataset
	for (size_t i = 0; i < numObjects; i++) {
		// Get the name of the dataset at index i
		try {
			std::string asset_id = file.getObjnameByIdx(i);

			// if asset_ids is not empty and asset_id is not in asset_ids skip
			if (asset_ids.has_value() && std::find(asset_ids.value().begin(), asset_ids.value().end(), asset_id) == asset_ids.value().end())
			{
				continue;
			}

			H5::DataSet dataset = file.openDataSet(asset_id + "/data");
			H5::DataSpace dataspace = dataset.getSpace();
			H5::DataSet datasetIndex = file.openDataSet(asset_id + "/datetime");
			H5::DataSpace dataspaceIndex = datasetIndex.getSpace();
			
			std::optional<size_t> warmup = this->market_asset.has_value() ? this->market_asset.value().beta_lookback : std::nullopt;
			auto asset = std::make_shared<Asset>(asset_id, this->exchange_id, warmup);
			this->assets.push_back(asset);
			AGIS_DO_OR_RETURN(asset->load(
				dataset,
				dataspace,
				datasetIndex,
				dataspaceIndex,
				this->dt_format
			), bool);
			this->candles += asset->get_rows();
		}
		catch (H5::Exception& e) {
			return AgisResult<bool>(AGIS_EXCEP(e.getCDetailMsg()));
		}
		catch (const std::exception& e) {
			return AgisResult<bool>(AGIS_EXCEP(e.what()));
		}
		catch (...) {
			return AgisResult<bool>(AGIS_EXCEP("Unknown exception"));
		}
	}
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> Exchange::restore(
	std::optional<std::vector<std::string>> asset_ids,
	std::optional<MarketAsset> market_asset)
{
	this->market_asset = market_asset;

	// check if loading in a single h5 file
	if (!is_folder(this->source_dir))
	{
		std::filesystem::path path(this->source_dir);
		if(path.extension() == ".h5") {
			auto res = restore_h5(asset_ids);
			if(res.is_exception()) return AgisResult<bool>(AGIS_EXCEP(res.get_exception()));
		}
		else {
			return AgisResult<bool>(AGIS_EXCEP("Invalid source directory"));
		}
	}
	else {
		auto asset_files = files_in_folder(this->source_dir);

		// Loop over all fles in source folder and build each asset
		for (const auto& file : asset_files)
		{
			std::filesystem::path path(file);
			std::string asset_id = path.stem().string();

			// if asset_ids is not empty and asset_id is not in asset_ids skip
			if (asset_ids.has_value() && std::find(asset_ids.value().begin(), asset_ids.value().end(), asset_id) == asset_ids.value().end())
			{
				continue;
			}
			std::optional<size_t> warmup = this->market_asset.has_value() ? this->market_asset.value().beta_lookback : std::nullopt;
			auto asset = std::make_shared<Asset>(asset_id, this->exchange_id, warmup);

			this->assets.push_back(asset);
			AGIS_DO_OR_RETURN(asset->load(file, this->dt_format), bool);
			this->candles += asset->get_rows();
		}
	}

	// set the market asset pointer
	if (this->market_asset.has_value())
	{
		// find asset in assets with the same id as the market asset
		auto new_market_asset_ptr = std::find_if(
			this->assets.begin(),
			this->assets.end(),
			[&](const AssetPtr& asset) {
				return asset->get_asset_id() == this->market_asset.value().market_id;
			}
		);
		if (new_market_asset_ptr == this->assets.end())
		{
			return AgisResult<bool>(AGIS_EXCEP("Market asset not found"));
		}
		// build the beta vectors if the market asset has a beta lookback
		if (this->market_asset.value().beta_lookback.has_value())
		{
			for (auto& asset : this->assets)
			{
				asset->__set_beta(*new_market_asset_ptr, this->market_asset.value().beta_lookback.value());
			}
		}
	}

	return AgisResult<bool>(true);
}


//============================================================================
bool Exchange::__is_valid_order(std::unique_ptr<Order>& order) const
{
	AssetPtr asset = this->assets[order->get_asset_index()];
	if (!asset->__is_streaming) return false;
	if (order->get_order_type() != OrderType::MARKET_ORDER) {
		if (!order->get_limit().has_value())
		{
			return false;
		}
	}
	return true;
}

//============================================================================
void Exchange::__place_order(std::unique_ptr<Order> order)
{
	LOCK_GUARD;
	order->set_order_create_time(this->exchange_time);
	this->orders.push_back(std::move(order));
	UNLOCK_GUARD
}


//============================================================================
void Exchange::__process_orders(AgisRouter& router, bool on_close)
{
	LOCK_GUARD
	size_t i = 0;
	for (auto orderIter = this->orders.begin(); orderIter != this->orders.end(); )
	{
		auto& order = *orderIter;

		// make sure it is a valid order
		if (!this->__is_valid_order(order))
		{
			order->reject(this->exchange_time);
			router.place_order(std::move(*orderIter));
			orderIter = this->orders.erase(orderIter);
			continue;
		}
		this->__process_order(on_close, order);

		if (order->is_filled())
		{
			// fill the order's asset pointer then return to router
			router.place_order(std::move(*orderIter));
			orderIter = this->orders.erase(orderIter);
		}
		else
		{
			++orderIter;
		}
	}
	UNLOCK_GUARD
}


//============================================================================
void Exchange::__process_order(bool on_close, OrderPtr& order) {
	// make sure it is a valid order
	switch (order->get_order_type())
	{
	case OrderType::MARKET_ORDER:
		this->__process_market_order(order, on_close);
		break;
	case OrderType::LIMIT_ORDER:
		this->__process_limit_order(order, on_close);
	case OrderType::STOP_LOSS_ORDER:
		break;
	case OrderType::TAKE_PROFIT_ORDER:
		break;
	default:
		break;
	}
	// if the order is filled set the asset pointer
	if (order->get_order_state() == OrderState::FILLED) {
		order->__asset = this->assets[order->get_asset_index() - this->exchange_offset];
	}
}


//============================================================================
void Exchange::__process_market_order(std::unique_ptr<Order>& order, bool on_close)
{
	auto market_price = this->__get_market_price(order->get_asset_index(), on_close);
	if (market_price == 0.0f) return;
	order->fill(market_price, this->exchange_time);
}


//============================================================================
void Exchange::__process_limit_order(std::unique_ptr<Order>& order, bool on_close)
{
	// get the current market price
	auto market_price = this->__get_market_price(order->get_asset_index(), on_close);
	// if market price is 0 return	
	if (market_price == 0.0f) return;
	// if order is a buy order and the limit price is greater than the market price
	// then fill the order
	if (order->get_units() > 0 && order->get_limit().value() >= market_price)
	{
		order->fill(market_price, this->exchange_time);
	}
	// if order is a sell order and the limit price is less than the market price
	// then fill the order
	else if (order->get_units() < 0 && order->get_limit().value() <= market_price)
	{
		order->fill(market_price, this->exchange_time);
	}
}


//============================================================================
void Exchange::__set_volatility_lookback(size_t window_size)
{
	this->volatility_lookback = window_size;
	if (window_size == 0) return;
	for(auto& asset : this->assets)
	{
		asset->__set_volatility(window_size);
	}
}


//============================================================================
AGIS_API double Exchange::__get_market_price(size_t index, bool on_close) const
{
	auto const& asset = this->assets[index];
	if (!asset) return 0.0f;
	if (!asset->__is_streaming) return 0.0f;
	return asset->__get_market_price(on_close);
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
json ExchangeMap::to_json() const {
	json j;
	for (const auto& pair : this->exchanges) {
		j[pair.first] = pair.second->to_json();
	}
	return j;
}

//============================================================================
AgisResult<bool> ExchangeMap::restore_exchange(
	std::string const& exchange_id_,
	std::optional<std::vector<std::string>> asset_ids,
	std::optional<MarketAsset> market_asset
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
				return asset->get_asset_id() == market_asset.value().market_id;
			}
		);
		if (new_market_asset_ptr == this->assets.end())
		{
			UNLOCK_GUARD
			return AgisResult<bool>(AGIS_EXCEP("Market asset not found"));
		}
		auto& market_asset_struct = exchange->__get_market_asset_struct_ref();
		market_asset_struct.asset = *new_market_asset_ptr;
		market_asset_struct.market_index = (*new_market_asset_ptr)->get_asset_index();
	}
	
	UNLOCK_GUARD
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> ExchangeMap::new_exchange(
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
AGIS_API AgisResult<double> ExchangeMap::get_asset_beta(size_t index) const
{
	auto asset = this->get_asset(index);
	if (asset.is_exception()) return AgisResult<double>(asset.get_exception());
	return asset.unwrap()->get_beta();
}
 

//============================================================================
AGIS_API AgisResult<double> Exchange::get_asset_beta(size_t index) const
{
	auto asset = this->get_asset(index);
	if (asset.is_exception()) return AgisResult<double>(asset.get_exception());
	return asset.unwrap()->get_beta();
}


//============================================================================
AgisResult<size_t> Exchange::get_column_index(std::string const& col) const
{
	if (!this->headers.contains(col)) return AgisResult<size_t>(AGIS_EXCEP("missing col"));
	return AgisResult<size_t>(this->headers.at(col));
}


//============================================================================
AgisResult<AssetPtr> ExchangeMap::get_asset(std::string const&  asset_id) const
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
	if(asset_res.is_exception()) return AgisResult<AssetPtr>(asset_res.get_exception());
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
AGIS_API ExchangePtr const ExchangeMap::get_exchange(std::string const & exchange_id_) const 
{
	return this->exchanges.at(exchange_id_);
}


//============================================================================
bool ExchangeMap::asset_exists(std::string const&  asset_id) const
{
	if (this->asset_map.count(asset_id))
	{
		return true;
	}
	return false;
}

AGIS_API std::vector<std::string> ExchangeMap::get_exchange_ids() const
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
	if (!cutoff) return std::span(this->dt_index, this->dt_index_size);
	return std::span(this->dt_index, this->current_index - 1);
}


//============================================================================
AGIS_API double ExchangeMap::__get_market_price(std::string& asset_id, bool on_close) const
{
	auto index = this->asset_map.at(asset_id);
	auto const& asset = this->assets[index];
	if (!asset) return 0.0f;
	if (!asset->__is_streaming) return 0.0f;
	return asset->__get_market_price(on_close);
}


//============================================================================
AGIS_API AgisResult<AssetPtr const> ExchangeMap::__get_market_asset(Frequency freq) const
{
	if(!this->market_assets.contains(freq)) return AgisResult<AssetPtr const>(AGIS_EXCEP("No market asset found for frequency"));
	return AgisResult<AssetPtr const>(this->market_assets.at(freq));
}


//============================================================================
AGIS_API double ExchangeMap::__get_market_price(size_t asset_index, bool on_close) const
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
	return AgisResult<std::string>(this->assets[index]->get_asset_id());
}


//============================================================================
void ExchangeMap::__goto(long long datetime)
{
	for (auto& exchange_pair : this->exchanges)
	{
		exchange_pair.second->__goto(datetime);
	}

	// move the indivual asssets forward in time
	for (auto& asset : this->assets)
	{
		asset->__goto(datetime);
	}

	// move the exchanges dt index to the correct position
	for (size_t i = this->current_index; i < this->dt_index_size; i++)
	{
		if (this->dt_index[i] >= datetime)
		{
			this->current_index = i;
			break;
		}
	}
	this->step();
}


//============================================================================
void ExchangeMap::__reset()
{
	this->current_index = 0;

	// reset assets that were expired and bring them back in to view
	for (auto& asset : this->assets)
	{
		asset->__reset();
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
void ExchangeMap::__place_order(std::unique_ptr<Order> order)
{
	auto& asset = this->assets[order->get_asset_index()];
	auto& exchange = this->exchanges.at(asset->get_exchange_id());
	exchange->__place_order(std::move(order));
;}


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
AGIS_API void ExchangeMap::__build()
{
	size_t exchange_offset = 0;
	for (auto& exchange_pair : this->exchanges)
	{
		exchange_pair.second->build(exchange_offset);
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
	this->assets_expired.resize(this->assets.size());
	std::fill(assets_expired.begin(), assets_expired.end(), nullptr);
}


//============================================================================
AGIS_API bool ExchangeMap::step()
{
	if (this->current_index == this->dt_index_size)
	{
		return false;
	}

	this->current_time = this->dt_index[this->current_index];

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


//============================================================================
void ExchangeView::remove_allocation(size_t asset_index)
{
	std::optional<size_t> index = std::nullopt;
	for (size_t i = 0; i < this->view.size(); i++)
	{
		if (view[i].asset_index = asset_index)
		{
			index = i;
		}
	}
	if (index.has_value())
	{
		this->view.erase(this->view.begin() + index.value());
	}
}


//============================================================================
bool compareBySecondValueAsc(const ExchangeViewAllocation& a, const ExchangeViewAllocation& b) {
	return a.allocation_amount < b.allocation_amount;  // Compare in ascending order
}


//============================================================================
bool compareBySecondValueDesc(const ExchangeViewAllocation& a, const ExchangeViewAllocation& b) {
	return a.allocation_amount > b.allocation_amount;  // Compare in descending order
}


//============================================================================
void ExchangeView::sort(size_t N, ExchangeQueryType sort_type)
{
	if (view.size() <= N) { return; }
	switch (sort_type) {
		case(ExchangeQueryType::Default):
			view.erase(view.begin() + N, view.end());
			return;
		case(ExchangeQueryType::NSmallest):
			std::partial_sort(
				view.begin(),
				view.begin() + N,
				view.end(),
				compareBySecondValueAsc);
			view.erase(view.begin() + N, view.end());
			return;
		case(ExchangeQueryType::NLargest):
			std::partial_sort(
				view.begin(),
				view.begin() + N,
				view.end(),
				compareBySecondValueDesc);
			view.erase(view.begin() + N, view.end());
			return;
		case(ExchangeQueryType::NExtreme): {
			auto n = N / 2;
			std::partial_sort(view.begin(), view.begin() + n, view.end(), compareBySecondValueDesc);
			std::partial_sort(view.begin() + n, view.begin() + N, view.end(), compareBySecondValueAsc);
			view.erase(view.begin() + n, view.end() - n);
			return;
		}
	}
}


AGIS_API std::string ev_opp_to_str(ExchangeViewOpp ev_opp)
{
	switch (ev_opp) {
	case ExchangeViewOpp::UNIFORM:
		return "UNIFORM";
	case ExchangeViewOpp::LINEAR_DECREASE:
		return "LINEAR_DECREASE";
	case ExchangeViewOpp::LINEAR_INCREASE:
		return "LINEAR_INCREASE";
	default:
		return "UNKNOWN";
	}
	
}

AGIS_API std::string ev_query_type(ExchangeQueryType ev_query)
{
	switch (ev_query) {
	case ExchangeQueryType::Default:
		return "Default";
	case ExchangeQueryType::NLargest:
		return "NLargest";
	case ExchangeQueryType::NSmallest:
		return "NSmallest";
	case ExchangeQueryType::NExtreme:
		return "NExtreme";
	}
	return"";
}