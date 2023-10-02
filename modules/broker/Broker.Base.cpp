module;
#pragma once
#define _SILENCE_CXX23_DENORM_DEPRECATION_WARNING

#include <fstream>
#include <iterator>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include "AgisException.h"
#include "AgisStrategy.h"
#include "Exchange.h"

module Broker:Base;

import <mutex>;
import <expected>;
import <functional>;

namespace fs = std::filesystem;

using namespace rapidjson;

namespace Agis
{

//============================================================================
std::expected<bool, AgisException>
Broker::load_tradeable_assets(std::string json_string) noexcept
{
	// load rapidjson document from string
	Document d;
	d.Parse(json_string.c_str());

	// verify the document is an array
	if (!d.IsArray()) {
		return std::unexpected<AgisException>("found json that is not a json array");
	}

	// iterate over the array
	for (auto it = d.MemberBegin(); it != d.MemberEnd(); ++it)
	{
		// test if key "asset_type" exists
		if (!it->value.HasMember("asset_type")) {
			return std::unexpected<AgisException>("Found element that does not contain key \"asset_type\"");
		}
		// get the asset_id as a string or return an error
		if (!it->value.HasMember("asset_id")) {
			return std::unexpected<AgisException>("Found element that does not contain key \"asset_id\"");
		}

		// get the asset type as string and convert to enum
		std::string asset_type = it->value["asset_type"].GetString();
		auto asset_type_enum_opt = AssetTypeFromString(asset_type);
		if (!asset_type_enum_opt.has_value()) {
			return std::unexpected<AgisException>(asset_type_enum_opt.error());
		}

		// get the asset id as string, make sure it exists
		std::string asset_id = it->value["asset_id"].GetString();
		auto asset = this->_exchange_map->get_asset(asset_id);
		if (asset.is_exception()) {
			return std::unexpected<AgisException>(asset.get_exception());
		}
		
		// not allowed to overwrite existing tradeable asset
		if (this->tradeable_assets.contains(asset.unwrap()->get_asset_index())) {
			return std::unexpected<AgisException>("Asset with id " + asset_id + " already exists");
		}

		// create tradeable asset
		TradeableAsset tradeable_asset;
		tradeable_asset.asset = asset.unwrap().get();
		tradeable_asset.is_margin_pct = it->value.HasMember("is_margin_pct") ? it->value["is_margin_pct"].GetBool() : true;
		tradeable_asset.unit_multiplier = it->value.HasMember("unit_multiplier") ? it->value["unit_multiplier"].GetUint() : 1;
		tradeable_asset.intraday_initial_margin = it->value.HasMember("intraday_initial_margin") ? it->value["intraday_initial_margin"].GetDouble() : 1;
		tradeable_asset.intraday_maintenance_margin = it->value.HasMember("intraday_maintenance_margin") ? it->value["intraday_maintenance_margin"].GetDouble() : 1;
		tradeable_asset.overnight_initial_margin = it->value.HasMember("overnight_initial_margin") ? it->value["overnight_initial_margin"].GetDouble() : 1;
		tradeable_asset.overnight_maintenance_margin = it->value.HasMember("overnight_maintenance_margin") ? it->value["overnight_maintenance_margin"].GetDouble() : 1;
		tradeable_asset.short_overnight_initial_margin = it->value.HasMember("short_overnight_initial_margin") ? it->value["short_overnight_initial_margin"].GetDouble() : 1;
		tradeable_asset.short_overnight_maintenance_margin = it->value.HasMember("short_overnight_maintenance_margin") ? it->value["short_overnight_maintenance_margin"].GetDouble() : 1;
		this->tradeable_assets.insert({ tradeable_asset.asset->get_asset_index(), std::move(tradeable_asset)});
	}

	return true;
}

//============================================================================
std::expected<bool, AgisException>
Broker::load_tradeable_assets(fs::path p) noexcept
{
	std::lock_guard<std::mutex> broker_lock(this->_broker_mutex);

	// verify path exists
	if (!fs::exists(p)) {
		return std::unexpected<AgisException>("Path " + p.string() + " does not exist");
	}
	// verify it is json file
	if (p.extension() != ".json") {
		return std::unexpected<AgisException>("Path " + p.string() + " is not a json file");
	}
	// load it into a string
	std::ifstream ifs(p);
	std::string json_string((std::istreambuf_iterator<char>(ifs)),
				(std::istreambuf_iterator<char>()));
	return this->load_tradeable_assets(json_string);
}


//============================================================================
std::expected<bool, AgisException>
Broker::strategy_subscribe(AgisStrategy* strategy) noexcept
{
	std::lock_guard<std::mutex> broker_lock(this->_broker_mutex);
	auto index = strategy->get_strategy_index();
	std::mutex& position_mutex = this->strategy_locks[index];
	std::lock_guard<std::mutex> lock(position_mutex);
	auto it = this->strategies.find(index);
	if (it != this->strategies.end()) {
		return std::unexpected<AgisException>("Strategy with id " + strategy->get_strategy_id() + " already subscribed");
	}
	else {
		this->strategies.insert({index,strategy});
		return true;
	}
}


//============================================================================
std::expected<BrokerPtr, AgisException> BrokerMap::new_broker(std::string broker_id) noexcept
{
	auto broker = std::make_shared<Broker>(broker_id, this->_exchange_map);
	auto res = this->register_broker(broker);
	if (res.has_value()) return broker;
	else return std::unexpected<AgisException>(res.error());
}


//============================================================================
std::expected<bool, AgisException>
BrokerMap::register_broker(BrokerPtr new_broker) noexcept
{
	if(this->_broker_id_map.contains(new_broker->get_id())){
		return std::unexpected<AgisException>("Broker with id " + new_broker->get_id() + " already exists");
	}
	else {
		new_broker->set_broker_index(this->_broker_map.size());
		this->_broker_id_map.insert({new_broker->get_id(), new_broker->get_index()});
		this->_broker_map.emplace(new_broker->get_index(), new_broker);
		return true;
	}
}


//============================================================================
std::expected<BrokerPtr, AgisException>
BrokerMap::get_broker(std::string broker_id) noexcept
{
	if(this->_broker_id_map.contains(broker_id)){
		size_t broker_index = this->_broker_id_map.at(broker_id);
		return this->_broker_map.at(broker_index);
	}
	else {
		return std::unexpected<AgisException>("Broker with id " + broker_id + " does not exist");
	}
}

//============================================================================
void 
Broker::__on_order_fill(std::reference_wrapper<OrderPtr> new_order) noexcept
{
	// set the order's broker pointer on fill in order to manage open positions filled by the broker
	new_order.get()->__broker = this;
}


//============================================================================
void
BrokerMap::__on_order_fill(std::reference_wrapper<OrderPtr> new_order) noexcept
{
	auto broker_index = new_order.get()->get_broker_index();
	auto it = this->_broker_map.find(broker_index);
	if (it != this->_broker_map.end()) {
		it->second->__on_order_fill(new_order);
	}
	else { // should be unreachable. Famous last words
		new_order.get()->reject(0);
	}
}


//============================================================================
void
Broker::__validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept
{
	// test if order's underlying asset is tradeable in this broker instance
	auto asset_index = new_order.get()->get_asset_index();
	if (asset_index >= this->tradeable_assets.size() || !this->tradeable_assets.contains(asset_index)) {
		new_order.get()->reject(0);
		return;
	}
}


//============================================================================
void
BrokerMap::__validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept
{
	auto broker_index = new_order.get()->get_broker_index();
	auto it = this->_broker_map.find(broker_index);
	if(it != this->_broker_map.end()){
		it->second->__validate_order(new_order);
	}
	else {
		new_order.get()->reject(0);
	}
}

} // namespace Agis