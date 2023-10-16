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

class FutureTable;


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

class Derivative : public Asset {
public:
    template<typename... Args>
    Derivative(
		Args&&... args
	) : Asset(std::forward<Args>(args)...) {}
    
    virtual std::expected<bool, AgisException> set_last_trade_date(std::shared_ptr<TradingCalendar> calendar) = 0;
    std::optional<long long> get_last_trade_date() const noexcept { return this->_last_trade_date; }
    bool expirable() const noexcept { return this->_last_trade_date.has_value(); }

protected:
    std::optional<long long> _last_trade_date;
};


//============================================================================
class Future : public Derivative {
    friend class FutureTable;
public:
    AGIS_API Future(
        std::string asset_id,
        std::string exchange_id,
        std::optional<size_t> warmup = std::nullopt,
        Frequency freq = Frequency::Day1,
        std::string time_zone = "America/New_York"
    ): Derivative(AssetType::US_FUTURE, asset_id, exchange_id, warmup, freq, time_zone) {}


private:
    bool __is_last_view(long long t) const noexcept override;
    [[nodiscard]] std::expected<bool, AgisException> __set_volatility(size_t lookback) override;
    [[nodiscard]] std::expected<bool, AgisException> __build(Exchange const* exchange) noexcept override;
    std::expected<bool, AgisException> set_future_code();
    std::expected<bool, AgisException> set_future_parent_contract();
    std::expected<bool, AgisException> set_last_trade_date(std::shared_ptr<TradingCalendar> calendar) override;
    FutureMonthCode _month_code;
    FutureParentContract _parent_contract;
    FutureTable const* _table = nullptr;
};


//============================================================================
class FutureTable : public AssetTable {
public:
    using AssetTable::const_iterator;

    FutureTable(
        Exchange* exchange,
        std::string contract_id);
	~FutureTable();

    [[nodiscard]] AGIS_API std::expected<FuturePtr, AgisErrorCode> front_month();
    std::string const& name() const override { return this->_contract_id; }
    [[nodiscard]] std::expected<bool, AgisException> __build() override; 
    
    std::vector<double> const& get_continous_close_vec() const noexcept { return this->_continous_close_vec; }
    std::vector<long long> const& get_continous_dt_vec() const noexcept { return this->_continous_dt_vec; }

    void __set_child_ptrs() const noexcept;

private:
    std::vector<double> _continous_close_vec;
    std::vector<long long> _continous_dt_vec;
    FuturePrivate* p = nullptr;
};

}