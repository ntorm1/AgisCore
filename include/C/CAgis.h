#pragma once
#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "Hydra.h"
#include "Exchange.h"


extern "C" {

AGIS_API const char* __get_exchange_id(Exchange* exchange);

AGIS_API Hydra * __new_hydra();

AGIS_API void __delete_hydra(Hydra * hydra);

AGIS_API AgisStatusCode __new_exchange(
	Hydra* hydra,
	AssetType asset_type,
	const char* exchange_id,
	const char* source_dir,
	Frequency freq,
	const char* dt_format
);

AGIS_API size_t __get_exchange_count(Hydra* hydra);
AGIS_API Exchange** __get_exchange_array(Hydra* hydra);
AGIS_API void __delete_exchange_array(Exchange** exchange_array, size_t size);

};