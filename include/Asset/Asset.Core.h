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

AGIS_API std::expected <AssetType, AgisException> AssetTypeFromString(std::string asset_type_str);
AGIS_API Frequency StringToFrequency(const std::string& valueStr);
AGIS_API const char* FrequencyToString(Frequency value);
AGIS_API AssetType StringToAssetType(const std::string& valueStr);

}