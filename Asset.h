#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"

#include <string>
#include <filesystem>
#include <unordered_map>

#include <arrow/api.h>
#include <H5Cpp.h>

#include "Utils.h"
#include "AgisErrors.h"
#include "AgisPointers.h"
#include "json.hpp"

enum class AGIS_API Frequency {
    Tick,    // Tick data
    Min1,    // 1 minute data
    Min5,    // 5 minute data
    Min15,   // 15 minute data
    Min30,   // 30 minute data
    Hour1,   // 1 hour data
    Hour4,   // 4 hour data
    Day1,    // 1 day data
};

NLOHMANN_JSON_SERIALIZE_ENUM(Frequency, {
    {Frequency::Tick, "Tick"},
    {Frequency::Min1, "Min1"},
    {Frequency::Min5, "Min5"},
    {Frequency::Min15, "Min15"},
    {Frequency::Min30, "Min30"},
    {Frequency::Hour1, "Hour1"},
    {Frequency::Hour4, "Hour4"},
    {Frequency::Day1, "Day1"},
    })

class Asset;

AGIS_API typedef std::shared_ptr<Asset> AssetPtr;

class  Asset
{
public: 
    AGIS_API Asset(
        std::string asset_id,
        std::string exchange_id,
        size_t warmup = 0,
        Frequency freq = Frequency::Day1,
        std::string time_zone = "America/New_York"
    );
    AGIS_API ~Asset();

    AGIS_API NexusStatusCode load(
        std::string source,
        std::string dt_fmt,
        std::optional<std::pair<long long, long long>> window = std::nullopt
    );

    AGIS_API NexusStatusCode load(
        H5::DataSet& dataset,
        H5::DataSpace& dataspace,
        H5::DataSet& datasetIndex,
        H5::DataSpace& dataspaceIndex
    );


    AGIS_API std::string get_asset_id() const { return this->asset_id; }
    AGIS_API size_t get_asset_index() const { return this->asset_index; }
    AGIS_API size_t const get_size() const { return this->rows - this->warmup; }
    AGIS_API size_t const get_rows() const { return this->rows; }
    AGIS_API size_t const get_cols() const { return this->columns; }
    AGIS_API size_t const get_warmup()const { return this->warmup; }
    AGIS_API size_t const get_current_index() const { return this->current_index - 1; }
    AGIS_API std::string const& get_exchange_id() { return this->exchange_id; }
    AGIS_API std::vector<std::string> get_column_names() const;
    AGIS_API std::unordered_map<std::string, size_t> const& get_headers() { return this->headers; };
    AGIS_API double get_asset_feature(std::string const& col, int index) const noexcept;

    AGIS_API double __get(std::string col, size_t row) const;
    AGIS_API long long __get_dt(size_t row) const { return *(this->dt_index + row); };
    AGIS_API size_t __get_open_index() const {return this->open_index;}
    AGIS_API size_t __get_close_index() const { return this->close_index; }
    
    AGIS_API double __get_market_price(bool on_close) const;
    AGIS_API AgisMatrix<double> const __get__data() const;
    AGIS_API StridedPointer<double> const __get_column(std::string const& column_name) const;
    AGIS_API StridedPointer<long long> const __get_dt_index() const;
    AGIS_API std::vector<std::string> __get_dt_index_str() const;

    bool __contains_column(std::string const& col) { return this->headers.count(col) > 0; }
    bool __valid_row(int n)const { return abs(n) <= (this->current_index - 1); }

    void __set_index(size_t index_) { this->asset_index = index_; }
    void __set_exchange_offset(size_t offset) { this->exchange_offset = offset; }
    size_t __get_index(bool offset = true) const { return offset ? this->asset_index : this->asset_index - this->exchange_offset; }


    AGIS_API void __set_alignment(bool is_aligned_) { this->__is_aligned = is_aligned_; }
    bool __in_warmup() { return (this->current_index - 1) < this->warmup; }
    void __set_warmup(size_t warmup_) { if (this->warmup < warmup_) this->warmup = warmup_;}
    bool __is_aligned = false;
    bool __is_streaming = false;
    bool __is_expired = false;
    bool __is_valid_next_time = true;
    void __step();
    bool __is_valid_time(long long& datetime);
    long long __get_asset_time() const { return this->dt_index[this->current_index];}
    bool __is_last_view() const { return this->current_index - 1 == this->rows; }


    void __goto(long long datetime);
    void __reset();

private:
    bool is_loaded = false;
    std::string asset_id;

    size_t asset_index;
    size_t exchange_offset;

    std::string exchange_id;
    std::string source;
    std::string dt_fmt;
    std::string tz;
    size_t warmup;
    Frequency freq;

    size_t rows          = 0;
    size_t columns       = 0;
    size_t current_index = 0;
    size_t open_index;
    size_t close_index;
    long long* dt_index;
    double* data;
    double* close;
    double* open;
    std::optional<std::pair<long long, long long>> window;

    std::unordered_map<std::string, size_t> headers;

    NexusStatusCode load_headers();
    NexusStatusCode load_csv();
    const arrow::Status load_parquet();

};

// Function to convert a string to Frequency enum
AGIS_API Frequency string_to_freq(const std::string& str);