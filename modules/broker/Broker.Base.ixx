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
import <atomic>;
import <unordered_map>;

import <expected>;
import <functional>;


class AgisStrategy;

typedef std::unique_ptr<Order> OrderPtr;

export namespace Agis 
{


class Broker
{
	friend class AgisStrategy;
public:
	Broker() = delete;
	Broker(
		std::string broker_id
	) {
		_broker_id = broker_id;
		_broker_index = broker_id_counter++;
	};
	virtual ~Broker() = default;


	std::optional<std::reference_wrapper<OrderPtr>> __validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept;

	[[nodiscard]] std::string const& get_id() const noexcept { return _broker_id; };
	[[nodiscard]] size_t get_index() const noexcept { return _broker_index; };

protected:
	std::expected<bool, AgisException> strategy_subscribe(size_t strategy_id) noexcept;
	std::expected<bool, AgisException> deposit_cash(size_t strategy_id, double amount) noexcept;

private:
	AGIS_API static std::atomic<uint64_t> broker_id_counter;

	std::string _broker_id;
	size_t _broker_index;

	std::unordered_map<size_t, std::mutex> strategy_locks;	///< Locks for each strategy
	ankerl::unordered_dense::map<size_t, double> deposits; ///< Cash deposits in the broker's account
	
	double _interest_rate;	///< Interest rate on cash held in the broker's account
	double _margin_rate;	///< Margin rate charged on margin debt
};

using BrokerPtr = std::shared_ptr<Broker>;

class BrokerMap
{
public:
	BrokerMap() = default;
	~BrokerMap() = default;

	std::optional<std::reference_wrapper<OrderPtr>> __validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept;
	AGIS_API std::expected<bool, AgisException> register_broker(BrokerPtr new_broker) noexcept;
	AGIS_API std::expected<BrokerPtr, AgisException> get_broker(std::string broker_id) noexcept;

private:
	ankerl::unordered_dense::map<std::string, size_t> _broker_id_map;
	ankerl::unordered_dense::map<size_t, BrokerPtr> _broker_map;
};

} // namespace Agis