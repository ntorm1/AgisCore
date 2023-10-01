module;

#include "AgisException.h"

module Broker:Base;

import <mutex>;
import <expected>;
import <functional>;


namespace Agis
{


//============================================================================
std::expected<bool, AgisException>
Broker::strategy_subscribe(size_t strategy_id) noexcept
{
	std::mutex& position_mutex = this->strategy_locks[strategy_id];
	std::lock_guard<std::mutex> lock(position_mutex);
	auto it = this->deposits.find(strategy_id);
	if (it != this->deposits.end()) {
		return std::unexpected<AgisException>("Strategy with id " + std::to_string(strategy_id) + " already subscribed");
	}
	else {
		this->deposits.insert({strategy_id, 0.0});
		return true;
	}
}


//============================================================================
std::expected<bool, AgisException>
Broker::deposit_cash(size_t strategy_id, double amount) noexcept
{
	std::mutex& position_mutex = this->strategy_locks[strategy_id];
	std::lock_guard<std::mutex> lock(position_mutex);
	auto it = this->deposits.find(strategy_id);
	if (it == this->deposits.end()) {
		return std::unexpected<AgisException>("Strategy with id " + std::to_string(strategy_id) + " not subscribed");
	}
	else {
		it->second += amount;
	}
	return true;
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
		this->_broker_map.emplace(new_broker->get_index(), std::move(new_broker));
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
Broker::__validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept
{
	// test if order's underlying asset is tradeable in this broker instance
	auto asset_index = new_order.get()->get_asset_index();
	if (asset_index >= this->tradeable_assets.size() || !this->tradeable_assets.at(asset_index)) {
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


//============================================================================
void
Broker::add_tradeable_assets(size_t asset_index) noexcept
{
	// Resize the tradeable_assets vector if needed
	if (asset_index >= tradeable_assets.size()) {
		tradeable_assets.resize(asset_index + 1, false);
	}

	// Set the specified asset index to true
	tradeable_assets.at(asset_index) = true;
}


//============================================================================
void
Broker::add_tradeable_assets(std::vector<size_t> asset_indices) noexcept
{
	size_t new_size = 0;
	for (auto asset_index : asset_indices) {
		new_size = std::max(new_size, asset_index + 1);
	}

	// Resize the tradeable_assets vector if needed
	if (new_size > tradeable_assets.size()) {
		tradeable_assets.resize(new_size, false);
	}

	// Set the specified asset indices to true
	for (auto asset_index : asset_indices) {
		tradeable_assets.at(asset_index) = true;
	}
}

} // namespace Agis