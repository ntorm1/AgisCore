export module Broker:Dummy;

import :Base;

export namespace Agis
{
	class DummyBroker : public Broker
	{
		DummyBroker(std::string broker_id, double cash) : Broker(broker_id, cash) {}
		~DummyBroker() = default;
	};
}