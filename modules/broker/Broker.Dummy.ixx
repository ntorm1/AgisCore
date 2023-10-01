export module Broker:Dummy;

import :Base;

export namespace Agis
{
	class DummyBroker : public Broker
	{
		DummyBroker(std::string broker_id) : Broker(broker_id) {}
		~DummyBroker() = default;
	};
}