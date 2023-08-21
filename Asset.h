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

#define ARROW_API_H
#define H5_HAVE_H5CPP
#ifdef H5_HAVE_H5CPP
#include <H5Cpp.h>
#endif

#ifdef ARROW_API_H
#include <arrow/api.h>
#endif

#include "Utils.h"
#include "AgisErrors.h"
#include "AgisPointers.h"
#include "json.hpp"


/// <summary>
/// Enum for the data frequency of an asset. 
/// </summary>
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

/// <summary>
/// Serialization mapping for an asset's frequency
/// </summary>
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


/**
*   @par Usage
* 
*   Asset's are created directly from files and can take the form of csv, hdf5, or parquet.
*   For csv files, the data should like the following, note the string datetime index: 
*   
*   | Index                | Open   | Close |
*   |----------------------|--------|-------|
*   |     2019-01-01       |   10.10|10.12  |
*   |     2019-01-02       |   12.2 |10.13  |
*   |     2019-01-03       |    9.8 |10.14  |
*   
* 
*   To create an asset from hdf5 data, you need to datasets. One for the nanosecond epoch index,
*   and one for the actual data. Note the index should be an int64 nanosecond epoch index, not string.
*   Use the following python code as an example to see how to format the .h5 file so that we can read it.
*   
*   @code{.py}
*   _path = os.path.join(path, "hdf5", "data.h5")
*   with h5py.File(_path, "a") as file:
*       # Convert the DataFrame to a NumPy array
*       cols = ["open","high","low","close","volume"]
*       data = df_mid[cols].to_numpy()

*       # Create a new dataset and save the data
*       file.create_dataset(f"{ticker}/datetime", data=df_mid["ts_event"].to_numpy())
*       dataset = file.create_dataset(f"{ticker}/data", data=data)
*
*       # Store column names as attributes
*       for col_name in cols:
*           dataset.attrs[col_name] = col_name
*   @endcode
* 
*   Assets should not be created directly but should instead be created by restoring 
*   an exchange. That being said, if you want to create a new asset and load data you can 
*   do the following to create an asset instance and load the data. Note the the file extension
*   will automatically detect how to load the data, if it is invalid extension then you will get
*   an error return code on the load:
* 
*   @code
*   auto asset = std::make_shared<Asset>("asset_id", "exchange_id");
*   auto status = asset->load("C:/user/data/spy_daily/aapl.csv", "%Y-%m-%d");
*   @endcode
* 
*/
class  Asset
{
public:
    /// <summary>
    /// Asset constructor
    /// </summary>
    /// <param name="asset_id">unique id of the asset</param>
    /// <param name="exchange_id">id of the exchange the asset is placed on</param>
    /// <param name="warmup">number of rows of to skip before asset is visiable</param>
    /// <param name="freq">the frequency of the asset's data</param>
    /// <param name="time_zone">the time zone of the asset</param>
    /// <returns></returns>
    AGIS_API Asset(
        std::string asset_id,
        std::string exchange_id,
        size_t warmup = 0,
        Frequency freq = Frequency::Day1,
        std::string time_zone = "America/New_York"
    );
    AGIS_API ~Asset();

    /// <summary>
    /// Load in a asset's data from a filepath. Supported types: csv, Parquet, HDF5.
    /// </summary>
    /// <param name="source">the file path of the data source</param>
    /// <param name="dt_fmt">the format of the datetime index</param>
    /// <param name="window">a range of valid times to load, in the form of seconds since midnight</param>
    /// <returns></returns>
    AGIS_API NexusStatusCode load(
        std::string source,
        std::string dt_fmt,
        std::optional<std::pair<long long, long long>> window = std::nullopt
    );
    
#ifdef H5_HAVE_H5CPP
    /// <summary>
    /// Load a asset data from an H5 dataset and datetime index.
    /// </summary>
    /// <param name="dataset">H5 dataset for the asset data</param>
    /// <param name="dataspace">H5 dataspace for the asset data matrix</param>
    /// <param name="datasetIndex">H5 dataset for the asset index</param>
    /// <param name="dataspaceIndex">H5 dataspace for the asset index</param>
    /// <returns></returns>
    AGIS_API NexusStatusCode load(
        H5::DataSet& dataset,
        H5::DataSpace& dataspace,
        H5::DataSet& datasetIndex,
        H5::DataSpace& dataspaceIndex
    );
#endif


    AGIS_API inline  std::string get_asset_id() const { return this->asset_id; }
    AGIS_API inline  size_t get_asset_index() const { return this->asset_index; }
    AGIS_API inline  size_t const get_size() const { return this->rows - this->warmup; }
    AGIS_API inline  size_t const get_rows() const { return this->rows; }
    AGIS_API inline  size_t const get_cols() const { return this->columns; }
    AGIS_API inline  size_t const get_warmup()const { return this->warmup; }
    AGIS_API inline  size_t const get_current_index() const { return this->current_index - 1; }
    AGIS_API inline  std::string const& get_exchange_id() { return this->exchange_id; }
    AGIS_API std::vector<std::string> get_column_names() const;
    AGIS_API inline std::unordered_map<std::string, size_t> const& get_headers() { return this->headers; };
    AGIS_API AgisResult<double> get_asset_feature(std::string const& col, int index) const;

    AGIS_API double __get(std::string col, size_t row) const;
    AGIS_API inline long long __get_dt(size_t row) const { return *(this->dt_index + row); };
    AGIS_API inline size_t __get_open_index() const {return this->open_index;}
    AGIS_API inline size_t __get_close_index() const { return this->close_index; }
    
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


    AGIS_API inline void __set_alignment(bool is_aligned_) { this->__is_aligned = is_aligned_; }
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