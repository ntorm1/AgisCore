#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include <string>

#include "Asset/Asset.Core.h"
#include "Asset/Asset.Base.h"

namespace Agis {

class Equity : public Asset {
public:
    AGIS_API Equity(
        std::string asset_id,
        std::string exchange_id,
        std::optional<size_t> warmup = std::nullopt,
        Frequency freq = Frequency::Day1,
        std::string time_zone = "America/New_York"
    ) : Asset(AssetType::US_EQUITY, asset_id, exchange_id, warmup, freq, time_zone) {}
};

}