export module Broker:Base;

import <ankerl/unordered_dense.h>;
import <memory>;

export import <expected>;
export import <functional>;
export import <AgisException.h>;

export namespace Agis 
{


class Broker
{
public:
	Broker(std::string broker_id) {
		_broker_id = broker_id;
		_broker_index = broker_id_counter++;
	};
	virtual ~Broker() = default;

	[[nodiscard]] std::string get_id() const noexcept { return _broker_id; };
	[[nodiscard]] size_t get_index() const noexcept { return _broker_index; };

private:
	static size_t broker_id_counter;

	std::string _broker_id;
	size_t _broker_index;

};


class BrokerMap
{
public:
	using BrokerPtr = std::unique_ptr<Broker>;

	BrokerMap() = default;
	~BrokerMap() = default;

	std::expected<bool, AgisException> register_broker(BrokerPtr new_broker) noexcept;
	std::expected<std::reference_wrapper<const Broker>, AgisException> get_broker(std::string broker_id) const noexcept;

private:
	ankerl::unordered_dense::map<std::string, size_t> _broker_id_map;
	ankerl::unordered_dense::map<size_t, BrokerPtr> _broker_map;
};

} // namespace Agis