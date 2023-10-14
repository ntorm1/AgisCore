#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include <string>
#include <memory>
#include <expected>
#include <optional>
#include <vector>
#include <memory>
#include <expected>

#include "AgisException.h"


#include "Asset/Asset.Core.h"
#include "Asset/Asset.Base.h"
#include "Asset/Asset.Table.h"

namespace Agis {


struct FuturePrivate;
class Asset;
class Future;
class TradingCalendar;
typedef std::shared_ptr<Asset> AssetPtr;
typedef std::shared_ptr<Future> FuturePtr;


enum class FutureMonthCode : uint8_t {
	F = 1,
	G = 2,
	H = 3,
	J = 4,
	K = 5,
	M = 6,
	N = 7,
	Q = 8,
	U = 9,
	V = 10,
	X = 11,
	Z = 12
};

enum class FutureParentContract : uint8_t {
	ES = 1,
    CL = 2,
    ZF = 3
};



//============================================================================
class Future : public Asset {

public:
    AGIS_API Future(
        std::string asset_id,
        std::string exchange_id,
        std::optional<size_t> warmup = std::nullopt,
        Frequency freq = Frequency::Day1,
        std::string time_zone = "America/New_York"
    ): Asset(AssetType::US_FUTURE, asset_id, exchange_id, warmup, freq, time_zone) {}


private:
    [[nodiscard]] std::expected<bool, AgisException> __build() noexcept override;
    std::expected<bool, AgisException> set_future_code();
    std::expected<bool, AgisException> set_future_parent_contract();
    std::expected<bool, AgisException> set_last_trade_date(std::shared_ptr<TradingCalendar> calendar);
    FutureMonthCode _month_code;
    FutureParentContract _parent_contract;
    long long _last_trade_date;
};


//============================================================================
class FutureTable : public AssetTable {
public:
    using AssetTable::iterator;
    using AssetTable::const_iterator;

    FutureTable(
        Exchange* exchange,
        std::string contract_id);
	~FutureTable();

    std::string const& name() const override { return this->_contract_id; }
    std::expected<bool, AgisException> build() override;

private:
	std::string _contract_id;

    FuturePrivate* p = nullptr;
};

}