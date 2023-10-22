#include "C/CAgis.h"


#include "ExchangeMap.h"

//============================================================================
const char* __get_exchange_id(Exchange* exchange)
{
	auto& id = exchange->get_exchange_id();
	return id.c_str();
}

//============================================================================
Hydra* __new_hydra()
{
	return new Hydra();
}


//============================================================================
void __delete_hydra(Hydra* hydra)
{
	delete hydra;
}


//============================================================================
AGIS_API AgisStatusCode __new_exchange(
	Hydra* hydra,
	AssetType asset_type,
	const char* exchange_id,
	const char* source_dir,
	Frequency freq,
	const char* dt_format
)
{
	auto res = hydra->new_exchange(
		asset_type,
		exchange_id,
		source_dir,
		freq,
		dt_format
	);
	if (!res.is_exception()) {
		return AgisStatusCode::OK;
	}
	return AgisStatusCode::INVALID_CONFIGURATION;
}


//============================================================================
size_t __get_exchange_count(Hydra* hydra)
{
	return hydra->get_exchanges().get_exchanges().size();
}


//============================================================================
Exchange** __get_exchange_array(Hydra* hydra)
{
	std::vector<Exchange*> exchanges;
	auto& res = hydra->get_exchanges();
	for (auto& exchange : res.get_exchanges()) {
		exchanges.push_back(exchange.get());
	}
	// alloocate array
	auto arr = new Exchange*[exchanges.size()];
	// copy
	for (size_t i = 0; i < exchanges.size(); ++i) {
		arr[i] = exchanges[i];
	}
	return arr;
}


//============================================================================
void __delete_exchange_array(Exchange** exchange_array, size_t size)
{
	if (exchange_array != nullptr) {
		delete[] exchange_array; // Delete the array itself, not the objects it points to
	}
}
