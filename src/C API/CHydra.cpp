#include "C/CAgis.h"
#include "Asset/Asset.Base.h"

#include "ExchangeMap.h"

//============================================================================
const char* __get_exchange_id(Exchange* exchange)
{
	auto& id = exchange->get_exchange_id();
	return id.c_str();
}

//============================================================================
AGIS_API size_t __get_asset_count(Exchange* exchange)
{
	return exchange->get_asset_count();
}

//============================================================================
AGIS_API Asset** __get_asset_array(Exchange* exchange)
{
std::vector<Asset*> assets;
	auto& res = exchange->get_assets();
	for (auto& asset : res) {
		assets.push_back(asset.get());
	}
	auto arr = new Asset*[assets.size()];
	for (size_t i = 0; i < assets.size(); ++i) {
		arr[i] = assets[i];
	}
	return arr;
}


//============================================================================
AGIS_API void __delete_asset_array(Asset** asset_array, size_t size)
{
	if (asset_array != nullptr) {
		delete[] asset_array; // Delete the array itself, not the objects it points to
	}
}


//============================================================================
const char* __get_asset_id(Asset* asset)
{
	auto& asset_id = asset->get_asset_id();
	return asset_id.c_str();
}


//============================================================================
AGIS_API CAssetView __get_asset_data(Asset* asset)
{
	CAssetView data;
	data.cols = asset->get_cols();
	data.rows = asset->get_rows();
	
	// allocate array and copy
	data.data = asset->__get__data().data();
	data.dt_index = asset->__get_dt_index().data();

	// allocate column names and copy
	data.col_names = new char*[data.cols];
	auto _col_names = asset->get_column_names();
	for (size_t i = 0; i < data.cols; ++i) {
			data.col_names[i] = new char[_col_names[i].size() + 1];
			strcpy(data.col_names[i], _col_names[i].c_str());
	}

	return data;

}


//============================================================================
void __delete_asset_view(CAssetView asset_data)
{
	// free the column names. All other data is owned by the asset
	for (size_t i = 0; i < asset_data.cols; ++i) {
		delete[] asset_data.col_names[i];
	}
	delete[] asset_data.col_names;
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
