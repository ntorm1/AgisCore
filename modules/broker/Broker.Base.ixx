module;
#include "Order.h"

export module Broker:Base;

import <ankerl/unordered_dense.h>;
import <memory>;

export import <expected>;
export import <functional>;
export import <AgisException.h>;


typedef std::unique_ptr<Order> OrderPtr;

export namespace Agis 
{


class Broker
{
public:
	Broker() = delete;
	Broker(
		std::string broker_id,
		double cash
	) {
		_cash = cash;
		_broker_id = broker_id;
		_broker_index = broker_id_counter++;
	};
	virtual ~Broker() = default;

	std::optional<std::reference_wrapper<OrderPtr>> __validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept;

	[[nodiscard]] std::string const& get_id() const noexcept { return _broker_id; };
	[[nodiscard]] size_t get_index() const noexcept { return _broker_index; };

private:
	static size_t broker_id_counter;

	std::string _broker_id;
	size_t _broker_index;

	double _cash;			///< Cash held in the broker's account
	double _interest_rate;	///< Interest rate on cash held in the broker's account
	double _margin_rate;	///< Margin rate charged on margin debt
};


class BrokerMap
{
public:
	using BrokerPtr = std::unique_ptr<Broker>;

	BrokerMap() = default;
	~BrokerMap() = default;

	std::optional<std::reference_wrapper<OrderPtr>> __validate_order(std::reference_wrapper<OrderPtr> new_order) noexcept;
	std::expected<bool, AgisException> register_broker(BrokerPtr new_broker) noexcept;
	std::expected<std::reference_wrapper<const Broker>, AgisException> get_broker(std::string broker_id) const noexcept;

private:
	ankerl::unordered_dense::map<std::string, size_t> _broker_id_map;
	ankerl::unordered_dense::map<size_t, BrokerPtr> _broker_map;
};

} // namespace Agis