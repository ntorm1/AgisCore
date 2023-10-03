module;

#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "AgisException.h"
#include "AgisEnums.h"
#include <expected>

export module Asset:Core;



export namespace Agis
{

std::expected <AssetType, AgisException> AssetTypeFromString(std::string asset_type_str) {
	if (asset_type_str == "US_EQUITY") {
		return AssetType::US_EQUITY;
	}
	else if (asset_type_str == "US_FUTURE") {
		return AssetType::US_FUTURE;
	}
	else {
		return std::unexpected<AgisException>("Invalid asset type string: " + asset_type_str);
	}
}

}