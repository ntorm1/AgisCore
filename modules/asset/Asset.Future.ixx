module;

#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include <string>
#include <memory>
#include <expected>

#include "AgisException.h"

export module Asset:Future;

import :Core;
import :Base;
import :Table;


export namespace Agis {


struct FuturePrivate;
class Asset;
class Future;
class TradingCalendar;
typedef std::shared_ptr<Asset> AssetPtr;
typedef std::shared_ptr<Future> FuturePtr;


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

    std::expected<bool, AgisException> set_last_trade_date(std::shared_ptr<TradingCalendar> calendar);

private:
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