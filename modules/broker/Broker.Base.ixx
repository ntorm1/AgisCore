export module Broker:Base;

import <ankerl/unordered_dense.h>;
import <memory>;

class Broker;
export using BrokerPtr = std::shared_ptr<Broker>;

export namespace Agis 
{


class Broker
{
public:
	Broker() = default;
	virtual ~Broker() = default;
};


class BrokerMap
{
	ankerl::unordered_dense::map<std::string, size_t*> broker_id_map;
	ankerl::unordered_dense::map<std::string, BrokerPtr> broker_map;
};

} // namespace Agis