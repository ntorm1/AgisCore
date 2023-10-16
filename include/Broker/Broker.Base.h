#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "AgisException.h"
#include <ankerl/unordered_dense.h>
#include "pch.h"
#include <shared_mutex>
#include <unordered_map>
#include <filesystem>
#include <functional>


namespace fs = std::filesystem;
struct TradeableAsset;
class ExchangeMap;
class AgisStrategy;
class Portfolio;
class AgisRouter;
class Order;

typedef std::unique_ptr<Order> OrderPtr;

namespace Agis 
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

struct BrokerPrivate;

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
	virtual ~Broker();

	void __on_order_fill(std::reference_wrapper<OrderPtr> new_order) noexcept;
	void __validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept;

	[[nodiscard]] AGIS_API std::expected<bool, AgisException> load_tradeable_assets(
		TradeableAsset* tradeable_asset,
		std::vector<size_t> const& asset_indecies
	) noexcept;
	[[nodiscard]] AGIS_API std::expected<bool, AgisException> load_tradeable_assets(std::string const& json_string) noexcept;
	[[nodiscard]] AGIS_API std::expected<bool, AgisException> load_tradeable_assets(fs::path p) noexcept;
	[[nodiscard]] std::expected<bool, AgisException> load_table_tradeable_assets(const rapidjson::Value* j);

	[[nodiscard]] AGIS_API bool trade_exists(size_t asset_index, size_t strategy_index) noexcept;
	[[nodiscard]] AGIS_API std::expected<double, AgisException> get_margin_requirement(size_t asset_index, MarginType margin_type) noexcept;
	[[nodiscard]] std::string const& get_id() const noexcept { return _broker_id; };
	[[nodiscard]] size_t get_index() const noexcept { return _broker_index; };
	[[nodiscard]] void set_order_impacts(std::reference_wrapper<OrderPtr> new_order) noexcept;

protected:
	std::expected<bool, AgisException> strategy_subscribe(AgisStrategy* strategy) noexcept;
	void set_broker_index(size_t broker_index) noexcept { _broker_index = broker_index; };
	std::expected<bool, AgisException> set_tradeable_asset(std::string const& asset_id, TradeableAsset* tradeable_asset) noexcept;

private:
	friend class BrokerMap;
	BrokerPrivate* p;

	std::shared_mutex _broker_mutex;
	std::string _broker_id;
	size_t _broker_index = 0;
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