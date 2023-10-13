module;
#pragma once
#define _SILENCE_CXX23_DENORM_DEPRECATION_WARNING

#include <string>
#include <memory>
#include <set>
#include "Exchange.h"

module Asset:Future;

import :Base;
import TradingCalendar;

namespace Agis
{


bool is_futures_valid_contract(const std::string& contract) {
	static const std::vector<std::string> valid_future_contracts = { "ZF", "CL", "ES" };
	for (const std::string& valid : valid_future_contracts) {
		if (contract == valid) {
			return true;
		}
	}
	return false;
}

//============================================================================
std::expected<bool, AgisException>
build_futures_tables(Exchange* exchange)
{
	// to build futures table exchange must have trading calendar
	if(!exchange->get_trading_calendar()) {
		return std::unexpected<AgisException>("Exchange does not have trading calendar");
	}

	auto asset_ids = exchange->get_asset_ids();
	// find all unique future contracts
	std::set<std::string> start_codes;
	for (auto asset_id : asset_ids)
	{
		start_codes.insert(asset_id.substr(0, 2));
	}
	// create a table for each contract parent
	for (auto& contract_parent : start_codes) {
		if (!is_futures_valid_contract(contract_parent)) {
			return std::unexpected<AgisException>("Invalid contract parent");
		}
		if (exchange->get_asset_table(contract_parent).has_value()) {
			continue;
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
	case(AssetType::US_FUTURE):
		return build_futures_tables(exchange);
	default:
		return std::unexpected<AgisException>("Unexpected asset type");
	}
}





}