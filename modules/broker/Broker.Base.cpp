module;

module Broker:Base;


namespace Agis
{

size_t Broker::broker_id_counter(0);


//============================================================================
std::expected<bool, AgisException>
BrokerMap::register_broker(BrokerPtr new_broker) noexcept
{
	if(this->_broker_id_map.contains(new_broker->get_id())){
		return std::unexpected<AgisException>("Broker with id " + new_broker->get_id() + " already exists");
	}
	else {
		this->_broker_id_map.insert({new_broker->get_id(), new_broker->get_index()});
		this->_broker_map.emplace(new_broker->get_index(), std::move(new_broker));
		return true;
	}
}


//============================================================================
std::expected<std::reference_wrapper<const Broker>, AgisException>
BrokerMap::get_broker(std::string broker_id) const noexcept
{
	if(this->_broker_id_map.contains(broker_id)){
		size_t broker_index = this->_broker_id_map.at(broker_id);
		return std::cref(*this->_broker_map.at(broker_index));
	}
	else {
		return std::unexpected<AgisException>("Broker with id " + broker_id + " does not exist");
	}
}


} // namespace Agis