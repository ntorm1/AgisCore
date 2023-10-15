#include <stdexcept>

#include "Asset/Asset.Core.h"

namespace Agis {


//============================================================================
Frequency StringToFrequency(const std::string& valueStr) {
	if (valueStr == "Tick") {
		return Frequency::Tick;
	}
	else if (valueStr == "Min1") {
		return Frequency::Min1;
	}
	else if (valueStr == "Min5") {
		return Frequency::Min5;
	}
	else if (valueStr == "Min15") {
		return Frequency::Min15;
	}
	else if (valueStr == "Min30") {
		return Frequency::Min30;
	}
	else if (valueStr == "Hour1") {
		return Frequency::Hour1;
	}
	else if (valueStr == "Hour4") {
		return Frequency::Hour4;
	}
	else if (valueStr == "Day1") {
		return Frequency::Day1;
	}
	else {
		// Handle unknown values if needed
		throw std::invalid_argument("Unknown frequency string: " + valueStr);
	}
}


//============================================================================
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


//============================================================================
const char* FrequencyToString(Frequency value) {
	switch (value) {
	case Frequency::Tick: return "Tick";
	case Frequency::Min1: return "Min1";
	case Frequency::Min5: return "Min5";
	case Frequency::Min15: return "Min15";
	case Frequency::Min30: return "Min30";
	case Frequency::Hour1: return "Hour1";
	case Frequency::Hour4: return "Hour4";
	case Frequency::Day1: return "Day1";
	default: return nullptr; // Handle unknown values if needed
	}
}


//============================================================================
AssetType StringToAssetType(const std::string& valueStr)
{
	if (valueStr == "US_EQUITY") {
		return AssetType::US_EQUITY;
	}
	else if (valueStr == "US_FUTURE") {
		return AssetType::US_FUTURE;
	}
	else {
		// Handle unknown values if needed
		throw std::invalid_argument("Unknown asset type string: " + valueStr);
	}
}

}