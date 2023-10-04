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

import <shared_mutex>;
import <expected>;
import <functional>;

import Asset;

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
	try {
		d.Parse(json_string.c_str());
	}
	catch (std::exception& e) {
		return std::unexpected<AgisException>(e.what());
	}

	// verify the document is an array
	if (!d.IsArray()) {
		return std::unexpected<AgisException>("found json that is not a json array");
	}

	// iterate over the array
	for (SizeType i = 0; i < d.Size(); ++i) 
	{
		const rapidjson::Value& it = d[i];
		if (!it.IsObject()) {
			return std::unexpected<AgisException>("found element in array is not a json object");
		}

		// test if key "asset_type" exists
		if (!it.HasMember("asset_type")) {
			return std::unexpected<AgisException>("Found element that does not contain key \"asset_type\"");
		}
		// get the asset_id as a string or return an error
		if (!it.HasMember("asset_id")) {
			return std::unexpected<AgisException>("Found element that does not contain key \"asset_id\"");
		}

		// get the asset type as string and convert to enum
		std::string asset_type = it["asset_type"].GetString();
		auto asset_type_enum_opt = AssetTypeFromString(asset_type);
		if (!asset_type_enum_opt.has_value()) {
			return std::unexpected<AgisException>(asset_type_enum_opt.error());
		}

		// get the asset id as string, make sure it exists
		std::string asset_id = it["asset_id"].GetString();
		auto asset = this->_exchange_map->get_asset(asset_id);
		if (asset.is_exception()) {
			return std::unexpected<AgisException>(asset.get_exception());
		}
		
		// not allowed to overwrite existing tradeable asset
		TradeableAsset tradeable_asset;
		tradeable_asset.asset = asset.unwrap().get();

		auto asset_index = tradeable_asset.asset->get_asset_index();
		if (this->tradeable_assets.contains(asset_index)) {
			return std::unexpected<AgisException>("Asset with id " + asset_id + " already exists");
		}

		// asset's unit multiplier must be the same across all brokers. I.e. a CL oil futures contract
		// allways represents 1000 barrels of oil. If the unit multiplier is different, return an error
		size_t exsisting_multiplier = tradeable_asset.asset->get_unit_multiplier();
		if (it.HasMember("unit_multiplier") && exsisting_multiplier && exsisting_multiplier != it["unit_multiplier"].GetUint()) {
			return std::unexpected<AgisException>("Asset with id " + asset_id + " already has a unit multiplier of " + std::to_string(exsisting_multiplier));
		}

		if (it.HasMember("unit_multiplier") &&
			it.HasMember("intraday_initial_margin") &&
			it.HasMember("intraday_maintenance_margin") &&
			it.HasMember("overnight_initial_margin") &&
			it.HasMember("overnight_maintenance_margin") &&
			it.HasMember("short_overnight_initial_margin") &&
			it.HasMember("short_overnight_maintenance_margin")) {

			// All members exist, populate the struct
			tradeable_asset.unit_multiplier = it["unit_multiplier"].GetUint();
			tradeable_asset.intraday_initial_margin = it["intraday_initial_margin"].GetDouble();
			tradeable_asset.intraday_maintenance_margin = it["intraday_maintenance_margin"].GetDouble();
			tradeable_asset.overnight_initial_margin = it["overnight_initial_margin"].GetDouble();
			tradeable_asset.overnight_maintenance_margin = it["overnight_maintenance_margin"].GetDouble();
			tradeable_asset.short_overnight_initial_margin = it["short_overnight_initial_margin"].GetDouble();
			tradeable_asset.short_overnight_maintenance_margin = it["short_overnight_maintenance_margin"].GetDouble();

			this->tradeable_assets.insert({ asset_index, std::move(tradeable_asset) });
		}
		else {
			return std::unexpected<AgisException>("Asset with id " + asset_id + " must specify all margin requirement");
		}
	}

	return true;
}


//============================================================================
std::expected<double, AgisException>
Broker::get_margin_requirement(size_t asset_index, MarginType margin_type) noexcept
{
	std::shared_lock<std::shared_mutex> lock(_broker_mutex);

	auto it = this->tradeable_assets.find(asset_index);
	if (it == this->tradeable_assets.end()) {
		return std::unexpected<AgisException>("Asset with index " + std::to_string(asset_index) + " does not exist");
	}
	switch (margin_type)
	{
		case MarginType::INTRADAY_INITIAL:
			return it->second.intraday_initial_margin;
		case MarginType::INTRADAY_MAINTENANCE:
			return it->second.intraday_maintenance_margin;
		case MarginType::OVERNIGHT_INITIAL:
			return it->second.overnight_initial_margin;
		case MarginType::OVERNIGHT_MAINTENANCE:
			return it->second.overnight_maintenance_margin;
		case MarginType::SHORT_OVERNIGHT_INITIAL:
			return it->second.short_overnight_initial_margin;
		case MarginType::SHORT_OVERNIGHT_MAINTENANCE:
			return it->second.short_overnight_maintenance_margin;
		default:
				return std::unexpected<AgisException>("Invalid margin type");
	}
}


//============================================================================
std::expected<bool, AgisException>
Broker::load_tradeable_assets(fs::path p) noexcept
{
	std::unique_lock<std::shared_mutex> lock(_broker_mutex);

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
	std::unique_lock<std::shared_mutex> broker_lock(_broker_mutex);
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
	std::shared_lock<std::shared_mutex> lock(_broker_mutex);

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