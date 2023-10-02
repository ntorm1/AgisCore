module;

#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include "AgisException.h"

export module Asset:Core;

import <expected>;

export namespace Agis
{

enum class AGIS_API AssetType
{
	US_EQUITY,
	US_FUTURE,
};

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