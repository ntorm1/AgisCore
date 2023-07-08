#include "pch.h" 
#include <execution>
#include <tbb/parallel_for_each.h>

#include "utils_array.h"
#include "Exchange.h"
#include "AgisRouter.h"


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
}

Exchange::~Exchange()
{
	if (this->is_built)
	{
		delete[] this->dt_index;
	}
}

json Exchange::to_json() const
{
	return json{ 
		{"exchange_id", this->exchange_id},
		{"source_dir", this->source_dir},
		{"freq", this->freq},
		{"dt_format", this->dt_format}
	};
}

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

StridedPointer<long long> const Exchange::__get_dt_index() const
{
	return StridedPointer(this->dt_index, this->dt_index_size, 1);
}

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

void Exchange::reset()
{
	this->current_index = 0;
}

void Exchange::build()
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
		this->candles += asset->get_rows();
	}

	this->is_built = true;
}

bool Exchange::step()
{
	// if the current index is the last then return false, all assets listed on this exchange
	// are done streaming their data
	if (this->current_index == this->dt_index_size)
	{
		return false;
	}

	// set exchange time to compare to assets
	this->exchange_time = this->dt_index[this->current_index];
	auto expired_asset_count = this->expired_assets.size();
	 
	// Define a lambda function that processes each asset
	auto process_asset = [&](auto& asset) {
		// if asset is expired skip
		if (asset->__is_expired)
		{
			return;
		}

		// if asset is alligned to exchange just step forward in time, clean up if needed 
		if (asset->__is_aligned) {
			asset->__step();
			// remove, alligned assets will never expire?
			if (asset->__is_last_view()) {
				this->expired_assets.push_back(asset->__get_index());
				asset->__is_expired = true;
			}
			return;
		}

		// test to see if this is the last row of data for the asset
		if (asset->__is_last_view()) {
			this->expired_assets.push_back(asset->__get_index());
			asset->__is_expired = true;
			return;
		}

		// get the asset's current time
		if (asset->__get_asset_time() == this->exchange_time)
		{
			// add asset to market view, step the asset forward in time
			asset->__is_streaming = true;
			asset->__step();
		}
		else
		{
			asset->__is_streaming = false;
		}
	};

	std::for_each(
		std::execution::par,
		this->assets.begin(),
		this->assets.end(),
		process_asset);

	// For assets that have expired, update the main exchange map
	if (expired_asset_count < this->expired_assets.size())
	{
		LOCK_GUARD
		for (size_t i = expired_asset_count; i < this->expired_assets.size(); i++)
		{
			auto asset_index = this->expired_assets[i];
			exchanges->__set_asset(asset_index, nullptr);
		}
		UNLOCK_GUARD

	}

	// move to next datetime and return true showing the market contains at least one
	// asset that is not done streaming
	this->current_index++;
	return true;
}

NexusStatusCode Exchange::restore()
{
	if (!is_folder(this->source_dir))
	{
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


void Exchange::__place_order(std::unique_ptr<Order> order)
{
	LOCK_GUARD;
	order->set_order_create_time(this->exchange_time);
	this->orders.push_back(std::move(order));
	UNLOCK_GUARD
}

void Exchange::__process_orders(AgisRouter& router, bool on_close)
{
	LOCK_GUARD
	size_t i = 0;
	for (auto orderIter = this->orders.begin(); orderIter != this->orders.end(); )
	{
		auto& order = *orderIter;

		// make sure it is a valid order
		if (!this->assets[order->get_asset_index()])
		{
			order->reject(this->exchange_time);
			router.place_order(std::move(*orderIter));
			orderIter = this->orders.erase(orderIter);
			continue;
		}

		switch (order->get_order_type())
		{
			case OrderType::MARKET_ORDER:
				this->__process_market_order(order, on_close);
				break;
			case OrderType::LIMIT_ORDER:
				break;
			case OrderType::STOP_LOSS_ORDER:
				break;
			case OrderType::TAKE_PROFIT_ORDER:
				break;
			default:
				break;
		}

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

void Exchange::__process_market_order(std::unique_ptr<Order>& order, bool on_close)
{
	auto market_price = this->__get_market_price(order->get_asset_index(), on_close);
	if (market_price == 0.0f) return;
	order->fill(market_price, this->exchange_time);
}

AGIS_API double Exchange::__get_market_price(size_t index, bool on_close) const
{
	auto const& asset = this->assets[index];
	if (!asset) return 0.0f;
	if (!asset->__is_streaming) return 0.0f;
	return asset->__get_market_price(on_close);
}

ExchangeMap::ExchangeMap()
{
}

ExchangeMap::~ExchangeMap()
{
	if (this->is_built)
	{
		delete[] this->dt_index;
	}
}


json ExchangeMap::to_json() const {
	json j;
	for (const auto& pair : this->exchanges) {
		j[pair.first] = pair.second->to_json();
	}
	return j;
}


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

std::vector<std::string> ExchangeMap::get_asset_ids(std::string const& exchange_id_) const
{
	return this->exchanges.at(exchange_id_)->get_asset_ids();
}
 
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

AGIS_API ExchangePtr const ExchangeMap::get_exchange(std::string exchange_id_)
{
	return this->exchanges.at(exchange_id_);
}

bool ExchangeMap::asset_exists(std::string const&  asset_id) const
{
	if (this->asset_map.count(asset_id))
	{
		return true;
	}
	return false;
}

AGIS_API long long ExchangeMap::get_datetime()
{
	if (this->current_index == 0)
	{
		return 0;
	}
	// current index is moved forward on step() so during the step the time is 
	// one index step back from where the current index is
	return this->dt_index[this->current_index - 1];
}

NexusStatusCode ExchangeMap::remove_exchange(std::string const& exchange_id_)
{
	if (!this->exchanges.count(exchange_id_))
	{
		return NexusStatusCode::InvalidId;
	}
	this->exchanges.erase(exchange_id_);
	return NexusStatusCode::Ok;
}

StridedPointer<long long> const ExchangeMap::__get_dt_index() const
{
	return StridedPointer(this->dt_index, this->dt_index_size, 1);
}

AGIS_API double ExchangeMap::__get_market_price(std::string& asset_id, bool on_close) const
{
	auto index = this->asset_map.at(asset_id);
	auto const& asset = this->assets[index];
	if (!asset) return 0.0f;
	if (!asset->__is_streaming) return 0.0f;
	return asset->__get_market_price(on_close);
}

AGIS_API double ExchangeMap::__get_market_price(size_t asset_index, bool on_close) const
{
	auto const& asset = this->assets[asset_index];
	if (!asset) return 0.0f;
	if (!asset->__is_streaming) return 0.0f;
	return asset->__get_market_price(on_close);
}

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
}

void ExchangeMap::__set_asset(size_t asset_index, std::shared_ptr<Asset> asset)
{
	this->assets[asset_index] = asset;
}

void ExchangeMap::__process_orders(AgisRouter& router, bool on_close)
{
	auto exchange_process = [&](auto& exchange) {
		exchange.second->__process_orders(router, on_close);
	};

	tbb::parallel_for_each(
		this->exchanges.begin(),
		this->exchanges.end(),
		exchange_process
	);
}

void ExchangeMap::__place_order(std::unique_ptr<Order> order)
{
	auto& asset = this->assets[order->get_asset_index()];
	auto& exchange = this->exchanges.at(asset->get_exchange_id());
	exchange->__place_order(std::move(order));
;}

AGIS_API void ExchangeMap::build()
{
	for (auto& exchange_pair : this->exchanges)
	{
		exchange_pair.second->build();
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
}

AGIS_API bool ExchangeMap::step()
{
	if (this->current_index == this->dt_index_size)
	{
		return false;
	}

	// Define a lambda function that processes each asset
	auto process_exchange = [&](auto& exchange_pair) {
		exchange_pair.second->step();
	};

	std::for_each(
		std::execution::par,
		this->exchanges.begin(),
		this->exchanges.end(),
		process_exchange);

	this->current_index++;
	return true;
}

void ExchangeMap::__clear()
{
	this->exchanges.clear();
}

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
