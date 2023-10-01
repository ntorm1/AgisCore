export module Broker:Dummy;

import :Base;

export namespace Agis
{
	class DummyBroker : public Broker
	{
		DummyBroker() = default;
		~DummyBroker() = default;
	};
}