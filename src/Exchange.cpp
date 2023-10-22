#include "pch.h" 
#include <execution>
#include <tbb/parallel_for_each.h>
#include <chrono>
#include <Windows.h>
#include <H5Cpp.h>

#include "utils_array.h"
#include "Exchange.h"
#include "AgisRouter.h"
#include "AgisRisk.h"

#include "Asset/Asset.h"
#include "Time/TradingCalendar.h"

using namespace Agis;

using namespace rapidjson;




std::atomic<size_t> Exchange::exchange_counter(0);
std::vector<std::string> exchange_view_opps = {
	"UNIFORM", "LINEAR_DECREASE", "LINEAR_INCREASE","CONDITIONAL_SPLIT","UNIFORM_SPLIT",
	"CONSTANT"
};


// create variadic template to handle multiple asset types
template<typename... Args>
AssetPtr create_asset(AssetType t, Args&&... args)
{
	switch (t)
	{
	case AssetType::US_EQUITY:
		return std::make_shared<Equity>(std::forward<Args>(args)...);
	case AssetType::US_FUTURE:
		return std::make_shared<Future>(std::forward<Args>(args)...);
	default:
		throw std::runtime_error("invalid asset type");
	}
}


//============================================================================
Exchange::Exchange(
		AssetType asset_type_,
		std::string exchange_id_, 
		std::string source_dir_,
		Frequency freq_,
		std::string dt_format_,
		ExchangeMap* exchanges_) : 
	exchanges(exchanges_)
{
	this->asset_type = asset_type_;
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
rapidjson::Document Exchange::to_json() const
{
	rapidjson::Document j(rapidjson::kObjectType);
	j.AddMember("exchange_id", rapidjson::StringRef(exchange_id.c_str()), j.GetAllocator());
	j.AddMember("source_dir", rapidjson::StringRef(source_dir.c_str()), j.GetAllocator());
	j.AddMember("freq", rapidjson::StringRef(FrequencyToString(this->freq)), j.GetAllocator());
	j.AddMember("dt_format", rapidjson::StringRef(dt_format.c_str()), j.GetAllocator());
	j.AddMember("volatility_lookback", volatility_lookback, j.GetAllocator());

	if (market_asset.has_value()) {
		rapidjson::Value market_asset_id(market_asset.value()->asset->get_asset_id().c_str(), j.GetAllocator());
		j.AddMember("market_asset", market_asset_id.Move(), j.GetAllocator());
		j.AddMember("market_warmup", market_asset.value()->beta_lookback.value(), j.GetAllocator());
	}
	else {
		j.AddMember("market_asset", "", j.GetAllocator());
		j.AddMember("market_warmup", 0, j.GetAllocator());
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
AGIS_API std::vector<size_t> Exchange::get_asset_indices() const
{
	std::vector<size_t> keys;
	keys.reserve(this->assets.size());  // Reserve space in the vector for efficiency

	// Iterate over the map and extract the keys
	for (const auto& asset : this->assets) {
		keys.push_back(asset->get_asset_index());
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
		if (!val.has_value()){
			if (!panic) continue;
			else AGIS_THROW("exchange view faileed");
		}
		auto v = val.value();
		if (std::isnan(v)) continue;
		view.emplace_back( asset->get_asset_index(), v );
		view.back().live = true;
	}
	if (view.size() == 1) { return exchange_view; }
	exchange_view.sort(number_assets, query_type);
	return exchange_view;
}


//============================================================================
AGIS_API ExchangeView Exchange::get_exchange_view(
	const std::function<std::expected<double, AgisStatusCode>(std::shared_ptr<Asset>const&)>& func,
	ExchangeQueryType query_type, 
	int N,
	bool panic,
	size_t warmup)
{
	auto number_assets = (N == -1) ? this->assets.size() : static_cast<size_t>(N);
	ExchangeView exchange_view(this, number_assets);
	auto& view = exchange_view.view;
	std::expected<double, AgisStatusCode> val;
	for (auto const& asset : this->assets)
	{
		if (!asset || !asset->__in_exchange_view) continue;	// asset not in view, or disabled
		if (!asset->__is_streaming) continue;				// asset is not streaming
		val = func(asset);
		if (!val.has_value()) {
			if (panic) AGIS_THROW("exchange view failed");
			else continue;
		}
		auto x = val.value();			
		// check if x is nan (asset filter operations will cause this)
		if(std::isnan(x)) continue;
		view.emplace_back(asset->get_asset_index(), x);
		view.back().live = true;
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
	this->market_asset = std::make_shared<MarketAsset>(market_asset_, beta_lookback);

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
AgisResult<AssetPtr> Exchange::__get_market_asset() const
{
	if(!this->market_asset.has_value()) return AgisResult<AssetPtr>(AGIS_EXCEP("market asset not set"));
	return AgisResult<AssetPtr>(this->market_asset.value()->asset);
}


//============================================================================
std::optional<std::shared_ptr<MarketAsset>> Exchange::__get_market_asset_struct() const noexcept
{
	if(this->market_asset.has_value()) return this->market_asset.value();
	return std::nullopt;
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
	for(auto& asset : this->assets)
	{
		asset->__reset(this->dt_index[0]);
	}
	for(auto& table : this->asset_tables){
		table.second->__sort_table();
		table.second->__reset();
		table.second->__sort_table();
	}
}


//============================================================================
std::expected<bool, AgisException> Exchange::build(size_t exchange_offset_)
{
	if (this->is_built)
	{
		delete[] this->dt_index;
	}

	// Generate date time index as sorted union of each asset's datetime index
	auto datetime_index_ = vector_sorted_union(
		this->assets,
		[](std::shared_ptr<Asset> const obj) -> long long const*
		{ 
			if (obj->get_rows() < obj->get_warmup()) return nullptr; // exclude invlaid assets
			return obj->__get_dt_index(true).data();
		},
		[](std::shared_ptr<Asset> const obj)
		{ 
			return obj->get_size();
		});
	this->dt_index = get<0>(datetime_index_);
	auto t0 = this->dt_index[0];
	this->dt_index_size = get<1>(datetime_index_);
	this->candles = 0;

	for (auto& asset : this->assets) {
		// test to see if asset is alligned with the exchage's datetime index
		// makes updating market view faster
		asset->__set_alignment(asset->get_rows() == this->dt_index_size);
		asset->__reset(t0);
		auto res = asset->__build(this);
		if (!res.has_value()) return res;

		//set the asset's exchange offset so indexing into the exchange's asset vector works
		asset->__set_exchange_offset(exchange_offset_);
		this->candles += asset->get_rows();
	}

	// build any asset tables
	for (auto& table : this->asset_tables) {
		auto res = table.second->__build();
		if (!res.has_value()) return res;
	}

	// disable all observers, force strategy to re-init them
	for (auto& obv : this->asset_observers) {
		obv->set_touch(false);
	}

	this->exchange_offset = exchange_offset_;
	this->is_built = true;
	return true;
}


//============================================================================
bool are_same_day(long long time1, long long time2) {
	// Convert nanoseconds to seconds
	time_t t1 = static_cast<time_t>(time1 / 1000000000);
	time_t t2 = static_cast<time_t>(time2 / 1000000000);

	// Convert to tm structs
	struct tm tm1, tm2;
	gmtime_s(&tm1, &t1);
	gmtime_s(&tm2, &t2);

	// Compare year, month, and day
	return tm1.tm_mday == tm2.tm_mday;
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

	// set eod flag on assets
	bool is_eod = false;
	if(this->current_index == this->dt_index_size - 1) is_eod = true;
	else if(!are_same_day(this->exchange_time, this->dt_index[this->current_index + 1])) is_eod = true;
	 
	// Define a lambda function that processes each asset
	auto process_asset = [&](auto& asset) {
		// if asset is expired skip
		if (!asset || asset->__is_expired)
		{
			return;
		}
		// set eod flag on asset
		asset->__is_eod = is_eod;

		// if asset is alligned to exchange just step forward in time, clean up if needed 
		if (asset->__is_aligned) {
			asset->__step();
			return;
		}

		// test to see if this is the last row of data for the asset
		if (asset->__is_last_view(this->exchange_time)) {
			auto index = asset->__get_index(true);
			expired_assets.push_back(index);
			asset->__is_expired = true;
			asset->__is_streaming = false;
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
	};

	std::for_each(
		this->assets.begin(),
		this->assets.end(),
		process_asset
	);
	std::for_each(
		this->asset_tables.begin(),
		this->asset_tables.end(),
		[&](auto& table) {
			table.second->step();
		}
	);

	// move to next datetime and return true showing the market contains at least one
	// asset that is not done streaming
	this->current_index++;
	return true;
}


//============================================================================
AgisResult<bool> Exchange::restore_h5(std::optional<std::vector<std::string>> asset_ids)
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
			
			std::optional<size_t> warmup = this->market_asset.has_value() ? this->market_asset.value()->beta_lookback : std::nullopt;
			auto asset = create_asset(
				this->asset_type,
				asset_id,
				this->exchange_id,
				warmup
			);
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
	std::optional<std::shared_ptr<MarketAsset>> market_asset)
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
			std::optional<size_t> warmup = this->market_asset.has_value() ? this->market_asset.value()->beta_lookback : std::nullopt;
			auto asset = create_asset(
				this->asset_type,
				asset_id,
				this->exchange_id,
				warmup
			);

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
				return asset->get_asset_id() == this->market_asset.value()->market_id;
			}
		);
		if (new_market_asset_ptr == this->assets.end())
		{
			return AgisResult<bool>(AGIS_EXCEP("Market asset not found"));
		}
		// build the beta vectors if the market asset has a beta lookback
		if (this->market_asset.value()->beta_lookback.has_value())
		{
			for (auto& asset : this->assets)
			{
				asset->__set_beta(*new_market_asset_ptr, this->market_asset.value()->beta_lookback.value());
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
void Exchange::__place_order(std::unique_ptr<Order> order) noexcept
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
		break;
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
void Exchange::__add_asset_table(AssetTablePtr&& table) noexcept
{
	this->asset_tables.emplace(table->name(), std::move(table));
}


//============================================================================
std::expected<bool, AgisException> Exchange::load_trading_calendar(std::string const& path)
{
	this->_calendar = std::make_shared<TradingCalendar>();
	return this->_calendar->load_holiday_file(path);
}


//============================================================================
double Exchange::__get_market_price(size_t index, bool on_close) const
{
	auto const& asset = this->assets[index];
	if (!asset) return 0.0f;
	if (!asset->__is_streaming) return 0.0f;
	return asset->__get_market_price(on_close);
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

//============================================================================
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


//============================================================================
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