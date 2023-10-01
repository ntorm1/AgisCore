module;

#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "Order.h"
#include "AgisException.h"


export module Broker:Base;

import <ankerl/unordered_dense.h>;
import <memory>;
import <mutex>;
import <unordered_map>;

import <expected>;
import <functional>;


class AgisStrategy;

typedef std::unique_ptr<Order> OrderPtr;

export namespace Agis 
{

class BrokerMap;

class Broker
{
	friend class AgisStrategy;
	friend class BrokerMap;
public:
	Broker() = delete;
	Broker(
		std::string broker_id
	) {
		_broker_id = broker_id;
	};
	virtual ~Broker() = default;

	void __validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept;

	AGIS_API void add_tradeable_assets(size_t asset_index) noexcept;
	AGIS_API void add_tradeable_assets(std::vector<size_t> asset_indices) noexcept;
	[[nodiscard]] std::string const& get_id() const noexcept { return _broker_id; };
	[[nodiscard]] size_t get_index() const noexcept { return _broker_index; };

protected:
	std::expected<bool, AgisException> strategy_subscribe(size_t strategy_id) noexcept;
	std::expected<bool, AgisException> deposit_cash(size_t strategy_id, double amount) noexcept;
	void set_broker_index(size_t broker_index) noexcept { _broker_index = broker_index; };
private:
	std::string _broker_id;
	size_t _broker_index = 0;

	std::unordered_map<size_t, std::mutex> strategy_locks;		///< Locks for each strategy
	ankerl::unordered_dense::map<size_t, double> deposits;		///< Cash deposits in the broker's account
	std::vector<bool> tradeable_assets;							///< Assets that can be traded through the broker

	double _interest_rate;	///< Interest rate on cash held in the broker's account
	double _margin_rate;	///< Margin rate charged on margin debt
};

using BrokerPtr = std::shared_ptr<Broker>;

class BrokerMap
{
public:
	BrokerMap() = default;
	~BrokerMap() = default;

	AGIS_API void __validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept;
	AGIS_API std::expected<bool, AgisException> register_broker(BrokerPtr new_broker) noexcept;
	AGIS_API std::expected<BrokerPtr, AgisException> get_broker(std::string broker_id) noexcept;

private:
	ankerl::unordered_dense::map<std::string, size_t> _broker_id_map;
	ankerl::unordered_dense::map<size_t, BrokerPtr> _broker_map;
};

} // namespace Agis