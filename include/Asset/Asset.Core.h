#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "AgisException.h"
#include "AgisEnums.h"
#include <expected>


namespace Agis
{

class Asset;

//============================================================================
struct TradeableAsset
{
	TradeableAsset() = default;
	Asset* asset = nullptr;
	uint16_t 	unit_multiplier = 1;
	double		intraday_initial_margin = 1;
	double		intraday_maintenance_margin = 1;
	double		overnight_initial_margin = 1;
	double		overnight_maintenance_margin = 1;
	double		short_overnight_initial_margin = 1;
	double		short_overnight_maintenance_margin = 1;
};

AGIS_API std::expected <AssetType, AgisException> AssetTypeFromString(std::string asset_type_str);
AGIS_API Frequency StringToFrequency(const std::string& valueStr);
AGIS_API const char* FrequencyToString(Frequency value);
AGIS_API AssetType StringToAssetType(const std::string& valueStr);

}