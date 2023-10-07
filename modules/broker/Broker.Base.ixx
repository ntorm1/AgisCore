module;

#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "Order.h"
#include "AgisException.h"
#include <ankerl/unordered_dense.h>

export module Broker:Base;

import <memory>;
import <shared_mutex>;
import <unordered_map>;
import <filesystem>;
import <expected>;
import <functional>;

typedef std::unique_ptr<Order> OrderPtr;

namespace fs = std::filesystem;

export class ExchangeMap;
export class Portfolio;
export class AgisRouter;

export namespace Agis 
{

class Asset;


enum class MarginType
{
	INTRADAY_INITIAL,
	INTRADAY_MAINTENANCE,
	OVERNIGHT_INITIAL,
	OVERNIGHT_MAINTENANCE,
	SHORT_OVERNIGHT_INITIAL,
	SHORT_OVERNIGHT_MAINTENANCE
};


//============================================================================
struct TradeableAsset
{
	Asset* asset;
	uint16_t 	unit_multiplier;
	double		intraday_initial_margin = 1;
	double		intraday_maintenance_margin = 1;
	double		overnight_initial_margin = 1;
	double		overnight_maintenance_margin = 1;
	double		short_overnight_initial_margin = 1;
	double		short_overnight_maintenance_margin = 1;
};


//============================================================================
class Broker
{
	friend class AgisStrategy;
public:
	Broker(
		std::string broker_id,
		AgisRouter* router,
		ExchangeMap* exchange_map
	);
	Broker() = delete;
	virtual ~Broker() = default;

	void __on_order_fill(std::reference_wrapper<OrderPtr> new_order) noexcept;
	void __validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept;

	[[nodiscard]] AGIS_API std::expected<bool, AgisException> load_tradeable_assets(std::string const& json_string) noexcept;
	[[nodiscard]] AGIS_API std::expected<bool, AgisException> load_tradeable_assets(fs::path p) noexcept;
	
	[[nodiscard]] AGIS_API bool trade_exists(size_t asset_index, size_t strategy_index) noexcept;

	[[nodiscard]] AGIS_API std::expected<double, AgisException> get_margin_requirement(size_t asset_index, MarginType margin_type) noexcept;
	[[nodiscard]] std::string const& get_id() const noexcept { return _broker_id; };
	[[nodiscard]] size_t get_index() const noexcept { return _broker_index; };
	[[nodiscard]] void set_order_impacts(std::reference_wrapper<OrderPtr> new_order) noexcept;

protected:
	std::expected<bool, AgisException> strategy_subscribe(AgisStrategy* strategy) noexcept;
	void set_broker_index(size_t broker_index) noexcept { _broker_index = broker_index; };

private:

	friend class BrokerMap;

	std::shared_mutex _broker_mutex;
	std::string _broker_id;
	size_t _broker_index = 0;

	ExchangeMap* _exchange_map;
	std::unordered_map<size_t, std::mutex> strategy_locks;					///< Locks for each strategy
	ankerl::unordered_dense::map<size_t, AgisStrategy*> strategies;			///< Strategies subscribed to the broker
	ankerl::unordered_dense::map<size_t, TradeableAsset> tradeable_assets;	///< Tradeable assets												///< Open trades held by the broker
	AgisRouter* _router;													///< Router for sending orders to the exchange

	double _cash = 0;
};

using BrokerPtr = std::shared_ptr<Broker>;


//============================================================================
class BrokerMap
{
public:
	BrokerMap(ExchangeMap* exchange_map) : _exchange_map(exchange_map) {};
	~BrokerMap() = default;

	AGIS_API void __on_order_fill(std::reference_wrapper<OrderPtr> new_order) noexcept;
	AGIS_API void __validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept;
	AGIS_API std::expected<BrokerPtr, AgisException> new_broker(AgisRouter* router, std::string broker_id) noexcept;
	AGIS_API std::expected<bool, AgisException> register_broker(BrokerPtr new_broker) noexcept;
	AGIS_API std::expected<BrokerPtr, AgisException> get_broker(std::string broker_id) noexcept;

private:
	ExchangeMap* _exchange_map;
	ankerl::unordered_dense::map<std::string, size_t> _broker_id_map;
	ankerl::unordered_dense::map<size_t, BrokerPtr> _broker_map;
};

} // namespace Agis