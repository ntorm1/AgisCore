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
std::expected<bool, AgisException> AssetTable::build()
{
	auto asset_ids = this->_exchange->get_asset_indices();
	bool expirable = false;
	for (auto const& asset_id : asset_ids) {
		auto asset = this->_exchange->get_asset(asset_id).unwrap();
		auto future = std::dynamic_pointer_cast<Future>(asset);
		if (!future) {
			return std::unexpected<AgisException>("Invalid asset type");
		}
		if (future->expirable()) {
			expirable = true;
		}
		if (asset->__is_streaming) {
			this->_tradeable.push_back(future);
		}
		else {
			this->_out_of_bounds.push_back(future);
		}
	}

	if (expirable){
		this->sort_expirable(this->_tradeable);
		this->sort_expirable(this->_out_of_bounds);
	}

	return true;
}


//============================================================================
void AssetTable::next()
{
	// pop expired assets off the front of the table
	while (!this->_tradeable.empty() && !this->_tradeable.front()->__is_streaming) {
		DerivativePtr asset = std::move(this->_tradeable.front());
		this->_tradeable.pop_front();
		this->_out_of_bounds.push_back(asset);
	}
	// move new assets from the out of bounds table into the tradeable table
	this->reset();
}


//============================================================================
void AssetTable::reset()
{
	// assets are reset before tables so loop through the out of bounds assets and 
	// move them back into the tradeable table if they are streaming
	while (!this->_out_of_bounds.empty() && this->_out_of_bounds.front()->__is_streaming) {
		DerivativePtr asset = std::move(this->_out_of_bounds.front());
		this->_out_of_bounds.pop_front();
		this->_tradeable.push_back(asset);
	}
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