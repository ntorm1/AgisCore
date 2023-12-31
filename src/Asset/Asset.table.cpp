#pragma once


#include <string>
#include <memory>
#include <set>
#include "Exchange.h"


#include "Time/TradingCalendar.h"
#include "Asset/Asset.Future.h"

namespace Agis
{


//============================================================================
static bool is_futures_valid_contract(const std::string& contract) {
	static const std::vector<std::string> valid_future_contracts = { "ZF", "CL", "ES" };
	for (const std::string& valid : valid_future_contracts) {
		if (contract == valid) {
			return true;
		}
	}
	return false;
}


//============================================================================
static std::expected<bool, AgisException>
build_futures_tables(Exchange* exchange)
{
	// to build futures table exchange must have trading calendar
	if (!exchange->get_trading_calendar()) {
		return std::unexpected<AgisException>("Exchange does not have trading calendar");
	}

	auto asset_ids = exchange->get_asset_ids();
	// find all unique future contracts
	std::set<std::string> start_codes;
	for (auto const& asset_id : asset_ids)
	{
		start_codes.insert(asset_id.substr(0, 2));
	}
	// create a table for each contract parent
	for (auto& contract_parent : start_codes) {
		if (!is_futures_valid_contract(contract_parent)) {
			return std::unexpected<AgisException>("Invalid contract parent");
		}
		if (exchange->get_asset_table<FutureTable>(contract_parent).has_value()) {
			return std::unexpected<AgisException>("Futures contract exists");
		}
		auto table = std::make_shared<FutureTable>(exchange, contract_parent);
		exchange->__add_asset_table(std::move(table));
	}
	return true;
}



//============================================================================
std::expected<bool, AgisException>
build_asset_tables(Exchange* exchange)
{
	switch (exchange->get_asset_type())
	{
	case(AssetType::US_EQUITY):
		return true;
	case(AssetType::US_FUTURE):
		return build_futures_tables(exchange);
	default:
		return std::unexpected<AgisException>("Unexpected asset type");
	}
}


//============================================================================
bool AssetTable::__is_valid_memeber(FuturePtr asset) const noexcept
{
	// if the first datetime of the asset is greater than the last trade date
	// it will prevent the table from stepping through time so it is excluded
	auto dt_index = asset->__get_dt_index();
	auto last_trade_data = asset->get_last_trade_date();
	if (last_trade_data.has_value() &&
		last_trade_data.value() <= dt_index.front()) {
		return false;
	}
	return true;
}

//============================================================================
void AssetTable::__sort_table() noexcept
{
	// if expirable sort the two deques based on their expiry date
	if (this->_expirable) {
		this->sort_expirable(this->_tradeable);
		this->sort_expirable(this->_out_of_bounds);
	}
}

//============================================================================
std::expected<bool, AgisException> AssetTable::__build()
{
	this->_tradeable.clear();
	this->_out_of_bounds.clear();

	auto asset_ids = this->_exchange->get_asset_indices();
	for (auto const& asset_id : asset_ids) {
		auto asset = this->_exchange->get_asset(asset_id).unwrap();

		// find asset with matching contract id
		std::string id = asset->get_asset_id();
		if (id.size() < 2) {
			continue;
		}
		if (id.substr(0,2) != this->_contract_id) {
			continue;
		}

		auto future = std::dynamic_pointer_cast<Future>(asset);
		if (!future) {
			return std::unexpected<AgisException>("Invalid asset type");
		}
		// validate it is valid memeber of a table 
		if (!this->__is_valid_memeber(future)) {
			continue;
		}
		// set expirable flag if any asset is expirable
		if (future->expirable()) {
			this->_expirable = true;
		}
		// if asset is currently streaming add it to the tradeable table else push to 
		// out of bounds table to be pulled in when it starts streaming
		if (asset->__is_streaming) {
			this->_tradeable.push_back(future);
		}
		else {
			this->_out_of_bounds.push_back(future);
		}
	}
	this->__sort_table();
	return true;
}


//============================================================================
void AssetTable::step()
{
	// pop expired assets off the front of the table
	while (
		!this->_tradeable.empty()
		&&
		this->_tradeable.front()->__is_expired
		) 
	{
		DerivativePtr asset = this->_tradeable.front();
		this->_tradeable.pop_front();
		this->_out_of_bounds.push_back(asset);
	}

	if (this->_out_of_bounds.empty()) {
		return;
	}
	// assets are reset before tables so loop through the out of bounds assets and 
	// move them back into the tradeable table if they are streaming

	// iterate through out of bounds and pop if streaming
	auto it = this->_out_of_bounds.begin();
	while (it != this->_out_of_bounds.end()) {
			if ((*it)->__is_streaming) {
				this->_tradeable.push_back(*it);
				it = this->_out_of_bounds.erase(it);
			}
			else {
				++it;
			}
		}


}


//============================================================================
void AssetTable::__reset()
{
	// reset the table by clearing all assets and readding them. 
	// TODO: this is not very efficient
	std::vector<DerivativePtr> assets;
	// take all asset from bothe deques
	assets.reserve(this->_tradeable.size() + this->_out_of_bounds.size());
	assets.insert(assets.end(), this->_tradeable.begin(), this->_tradeable.end());
	assets.insert(assets.end(), this->_out_of_bounds.begin(), this->_out_of_bounds.end());
	this->_tradeable.clear();
	this->_out_of_bounds.clear();
	for (auto const& asset : assets) {
		if (asset->__is_streaming) {
			this->_tradeable.push_back(asset);
		}
		else {
			this->_out_of_bounds.push_back(asset);
		}
	}
	this->__sort_table();
}


//============================================================================
std::vector<AssetPtr> AssetTable::all_assets() const noexcept
{
	std::vector<AssetPtr> assets;
	assets.reserve(this->_tradeable.size() + this->_out_of_bounds.size());
	for (auto const& asset : this->_tradeable) {
		assets.push_back(asset);
	}
	for (auto const& asset : this->_out_of_bounds) {
		assets.push_back(asset);
	}
	return assets;
}

//============================================================================
void AssetTable::sort_expirable(std::deque<DerivativePtr>& table) noexcept
{
	std::sort(
		table.begin(),
		table.end(),
		[](auto const& a, auto const& b) {
			const auto& dateA = a->get_last_trade_date();
			const auto& dateB = b->get_last_trade_date();

			if (dateA.has_value() && dateB.has_value()) {
				// Both have values, compare them
				return dateA.value() < dateB.value();
			}
			else if (dateA.has_value()) {
				// Only a has a value, a comes before b
				return true;
			}
			else if (dateB.has_value()) {
				// Only b has a value, b comes before a
				return false;
			}
			else {
				// Both are std::nullopt, consider them equal
				return false;
			}
		}
	);
}

}