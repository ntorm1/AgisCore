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

struct CAssetView {
	char** col_names;			
	const double* data;
	const long long* dt_index;
	size_t rows;
	size_t cols;
};

//==== Public Exchange C API ====//
AGIS_API const char* __get_exchange_id(Exchange* exchange);
AGIS_API size_t __get_asset_count(Exchange* exchange);
AGIS_API Asset** __get_asset_array(Exchange* exchange);
AGIS_API void __delete_asset_array(Asset** asset_array, size_t size);


//==== Public Asset C API ====//
AGIS_API const char* __get_asset_id(Asset* asset);
AGIS_API CAssetView __get_asset_data(Asset* asset);
AGIS_API void __delete_asset_view(CAssetView asset_data);

//==== Public Hydra C API ====//
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