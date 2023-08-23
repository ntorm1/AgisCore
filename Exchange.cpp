#include "pch.h" 
#include <execution>
#include <tbb/parallel_for_each.h>
#include <future>
#include <chrono>
#include <Windows.h>
#include "utils_array.h"
#include "Exchange.h"
#include "AgisRouter.h"


std::atomic<size_t> Exchange::exchange_counter(0);

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
json Exchange::to_json() const
{
	return json{ 
		{"exchange_id", this->exchange_id},
		{"source_dir", this->source_dir},
		{"freq", this->freq},
		{"dt_format", this->dt_format}
	};
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

	ExchangeView exchange_view(this->exchange_index, number_assets);
	auto& view = exchange_view.view;

	for (auto& asset : this->assets)
	{
		if (!asset) continue;
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
		view.push_back(std::make_pair(asset->get_asset_index(), v));
	}
	if (view.size() == 1) { return exchange_view; }
	exchange_view.sort(number_assets, query_type);
	return exchange_view;
}


//============================================================================
AGIS_API ExchangeView Exchange::get_exchange_view(
	const std::function<double(std::shared_ptr<Asset>const&)>& func,
	ExchangeQueryType query_type, 
	int N,
	bool panic,
	size_t warmup)
{
	auto number_assets = (N == -1) ? this->assets.size() : static_cast<size_t>(N);
	ExchangeView exchange_view(this->exchange_index, number_assets);
	auto& view = exchange_view.view;
	for (auto& asset : this->assets)
	{
		if (!asset) continue;								// asset not in view
		if (!asset->__is_streaming) continue;				// asset is not streaming
		if (asset->get_current_index() < warmup) continue;	// asset current index less than required warmup

		double val;
		try { val = func(asset); }
		catch (const std::exception&e) {
			if (panic) throw e;
			else continue;
		}

		view.push_back(std::make_pair(asset->get_asset_index(), val));
	}
	exchange_view.sort(number_assets, query_type);
	return exchange_view;
}


//============================================================================
StridedPointer<long long> const Exchange::__get_dt_index() const
{
	return StridedPointer(this->dt_index, this->dt_index_size, 1);
}


//============================================================================
AGIS_API bool Exchange::asset_exists(std::string asset_id)
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
	for (int i = this->current_index; i < this->dt_index_size; i++)
	{
		//is >= right?
		if (this->dt_index[i] >= datetime)
		{
			this->current_index = i;
			return;
		}
	}
}


//============================================================================
void Exchange::reset()
{
	this->current_index = 0;
}


//============================================================================
void Exchange::build(size_t exchange_offset)
{
	if (this->is_built)
	{
		delete[] this->dt_index;
	}

	// Generate date time index as sorted union of each asset's datetime index
	auto datetime_index_ = vector_sorted_union(
		this->assets,
		[](std::shared_ptr<Asset> const obj)
		{ 
			return obj->__get_dt_index().get();
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
		asset->__set_exchange_offset(exchange_offset);

		this->candles += asset->get_rows();
	}

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
			// add asset to market view, step the asset forward in time
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

	std::for_each(
		this->assets.begin(),
		this->assets.end(),
		process_asset);

	// move to next datetime and return true showing the market contains at least one
	// asset that is not done streaming
	this->current_index++;
	return true;
}


//============================================================================
AGIS_API NexusStatusCode Exchange::restore_h5()
{
	H5::H5File file(this->source_dir, H5F_ACC_RDONLY);
	int numObjects = file.getNumObjs();

	// Read data from each dataset
	for (int i = 0; i < numObjects; i++) {
		// Get the name of the dataset at index i
		std::string asset_id = file.getObjnameByIdx(i);
		H5::DataSet dataset = file.openDataSet(asset_id + "/data");
		H5::DataSpace dataspace = dataset.getSpace();
		H5::DataSet datasetIndex = file.openDataSet(asset_id + "/datetime");
		H5::DataSpace dataspaceIndex = datasetIndex.getSpace();

		auto asset = std::make_shared<Asset>(asset_id, this->exchange_id);
		this->assets.push_back(asset);
		asset->load(
			dataset,
			dataspace,
			datasetIndex,
			dataspaceIndex
		);
	}
	return NexusStatusCode::Ok;
}


//============================================================================
NexusStatusCode Exchange::restore()
{
	if (!is_folder(this->source_dir))
	{
		std::filesystem::path path(this->source_dir);
		if(path.extension() == ".h5") {
			return restore_h5();
		}
		return NexusStatusCode::InvalidArgument;
	}
	auto asset_files = files_in_folder(this->source_dir);

	// Loop over all fles in source folder and build each asset
	for (const auto& file : asset_files)
	{
		std::filesystem::path path(file);
		std::string asset_id = path.stem().string();
		auto asset = std::make_shared<Asset>(asset_id, this->exchange_id);

		this->assets.push_back(asset);
		asset->load(file, this->dt_format);
	}
	return NexusStatusCode::Ok;
}



bool Exchange::__is_valid_order(std::unique_ptr<Order>& order) const
{
	if (!this->assets[order->get_asset_index()]) return false;
	if (order->get_order_type() != OrderType::MARKET_ORDER &&
		!order->get_limit().has_value()) return false;
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
NexusStatusCode ExchangeMap::new_exchange(
	std::string exchange_id_,
	std::string source_dir_,
	Frequency freq_,
	std::string dt_format_)
{
	if (this->exchanges.count(exchange_id_))
	{
		return NexusStatusCode::InvalidId;
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

	// Load in the exchange's data
	exchange = this->exchanges.at(exchange_id_);
	auto res = exchange->restore();

	if (res != NexusStatusCode::Ok)
	{
		return res;
	}

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
	UNLOCK_GUARD

	return NexusStatusCode::Ok;
}


//============================================================================
std::vector<std::string> ExchangeMap::get_asset_ids(std::string const& exchange_id_) const
{
	return this->exchanges.at(exchange_id_)->get_asset_ids();
}
 

//============================================================================
std::optional<std::shared_ptr<Asset> const> ExchangeMap::get_asset(std::string const&  asset_id) const
{
#ifndef AGIS_SLOW
	if (this->asset_exists(asset_id))
	{
		return std::nullopt;
	}
#endif
	auto index = this->asset_map.at(asset_id);
	return this->assets[index];
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
StridedPointer<long long> const ExchangeMap::__get_dt_index() const
{
	return StridedPointer(this->dt_index, this->dt_index_size, 1);
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
AGIS_API double ExchangeMap::__get_market_price(size_t asset_index, bool on_close) const
{
	auto const& asset = this->assets[asset_index];
	if (!asset) return 0.0f;
	if (!asset->__is_streaming) return 0.0f;
	return asset->__get_market_price(on_close);
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
		if (this->dt_index[i] == datetime)
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

	auto current_time = this->dt_index[this->current_index];

	// set the exchagne time point 
	this->time_point = epoch_to_tp(current_time);

	expired_asset_index.clear();
	// Define a lambda function that processes each asset
	auto process_exchange = [&](auto& exchange_pair) {
		if (exchange_pair.second->__get_market_time() != current_time) { return; }
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
	// Process the exchange items in parallel
	std::for_each(std::execution::par, exchangeItems.begin(), exchangeItems.end(), [&](const auto& exchange) {
		auto const& exchange_id_ = exchange.first;
		auto const& exchange_json = exchange.second;
		auto const& source_dir_ = exchange_json["source_dir"];
		auto const& dt_format_ = exchange_json["dt_format"];
		auto freq_ = string_to_freq(exchange_json["freq"]);
		this->new_exchange(exchange_id_, source_dir_, freq_, dt_format_);
		});
}


bool compareBySecondValueAsc(const std::pair<size_t, double>& a, const std::pair<size_t, double>& b) {
	return a.second < b.second;  // Compare in ascending order
}

bool compareBySecondValueDesc(const std::pair<size_t, double>& a, const std::pair<size_t, double>& b) {
	return a.second > b.second;  // Compare in descending order
}

void ExchangeView::sort(size_t N, ExchangeQueryType sort_type)
{
	if (view.size() <= N) { return; }
	switch (sort_type) {
		case(ExchangeQueryType::Default):
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
}