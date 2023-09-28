#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"

#include <string>
#include <filesystem>
#include "AgisErrors.h"

#define ARROW_API_H
#define H5_HAVE_H5CPP

#ifdef H5_HAVE_H5CPP
#include <H5Cpp.h>
#endif

#ifdef ARROW_API_H
#include <arrow/api.h>
#endif

#ifndef USER_ANKERL
#include <ankerl/unordered_dense.h>
#endif // !USER_ANKERL

#include "Utils.h"
#include "AgisPointers.h"

class AssetObserver;

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
*   To create an asset from hdf5 data, you need two datasets. One for the nanosecond epoch index,
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

class Exchange;
class ExchangeMap;


class  Asset
{
    friend class Exchange;
    friend class ExchangeMap;

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
        std::optional<size_t> warmup = std::nullopt,
        Frequency freq = Frequency::Day1,
        std::string time_zone = "America/New_York"
    );
    AGIS_API ~Asset();

    /// <summary>
    /// Does the asset enclose another asset. This is used to determine if an asset is a valid asset
    /// for calculating beta. Enclose means that, if t0,t1 is first and last datetime of asset_b,
    /// then the two asset's datetime index's are the same.
    /// </summary>
    /// <param name="asset_b">the asset encloses asset_b</param>
    /// <returns></returns>
    AgisResult<bool> encloses(AssetPtr asset_b);

    /**
     * @brief given another asset which is enclosed, find the index location of the current asset
     * that marks the start of the datetime index of the enclosed asset
     * @param asset_b the child asset
     * @return index location of the first matching datetime index
    */
    AgisResult<size_t> encloses_index(AssetPtr asset_b);

    /**
     * @brief add a new asset observer if it does not already exist
     * @param observer the observer to add
    */
    AGIS_API void add_observer(AssetObserver* observer);

    /**
     * @brief remove an observer from the asset
     * @param observer 
    */
    AGIS_API void remove_observer(AssetObserver* observer);

    /**
     * @brief remove all observers from the asset
    */
    void clear_observers() { this->observers.clear(); }

    /// <summary>
    /// Takes an asset and returns a vector of NLV values representing the result of 
    /// a portfolio which takes a 100% position in this asset at time 0
    /// </summary>
    /// <param name="starting_amount"></param>
    /// <returns></returns>
    std::vector<double> generate_baseline_returns(double starting_amount);

    AGIS_API inline  std::string get_asset_id() const { return this->asset_id; }
    AGIS_API inline  size_t get_asset_index() const { return this->asset_index; }
    AGIS_API inline  size_t const get_size() const { return this->rows - this->warmup; }
    AGIS_API inline  size_t const get_rows() const { return this->rows; }
    AGIS_API inline  size_t const get_cols() const { return this->columns; }
    AGIS_API inline  size_t const get_warmup()const { return this->warmup; }
    AGIS_API inline Frequency const get_frequency() const { return this->freq; }
    AGIS_API size_t get_current_index() const;
    AGIS_API inline  std::string const& get_exchange_id() { return this->exchange_id; }
    AGIS_API std::vector<std::string> get_column_names() const;
    AGIS_API inline ankerl::unordered_dense::map<std::string, size_t> const& get_headers() { return this->headers; };
    AGIS_API void assign_asset_feature(size_t col, int index, AgisResult<double>& res);
    AGIS_API AgisResult<double> get_asset_feature(std::string const& col, int index) const;
    AGIS_API AgisResult<double> get_asset_feature(size_t col, int index) const;
    AGIS_API AgisResult<AssetObserver*> get_observer(std::string const& id) const noexcept;
    AGIS_API AgisResult<double> get_asset_observer_result(std::string const& observer_name) const noexcept;
    AGIS_API AgisResult<double> get_beta() const;
    AGIS_API AgisResult<double> get_volatility() const;
    AGIS_API const std::span<double const> get_beta_column() const;
    AGIS_API const std::span<double const> get_volatility_column() const;

    AGIS_API bool __get_is_valid_next_time() const { return __is_valid_next_time; }
    AGIS_API bool __is_last_row() const {return this->current_index == this->rows + 1;}
    AGIS_API double __get(std::string col, size_t row) const;
    AGIS_API inline long long __get_dt(size_t row) const { return *(this->dt_index + row); };
    AGIS_API inline size_t __get_open_index() const {return this->open_index;}
    AGIS_API inline size_t __get_close_index() const { return this->close_index; }
    
    AGIS_API double __get_market_price(bool on_close) const;
    AGIS_API AgisMatrix<double> const __get__data() const;
    AGIS_API std::span<double> const __get_column(size_t column_index) const;
    AGIS_API std::span<double> const __get_column(std::string const& column_name) const;
    AGIS_API std::span<long long> const __get_dt_index(bool adjust_for_warmup = true) const;
    AGIS_API std::vector<std::string> __get_dt_index_str(bool adjust_for_warmup = true) const;
    size_t __get_index(bool offset = true) const { return offset ? this->asset_index : this->asset_index - this->exchange_offset; }
    bool __get_is_aligned() const { return this->__is_aligned;}


    bool __contains_column(std::string const& col) { return this->headers.count(col) > 0; }
    bool __valid_row(int n)const { return abs(n) <= (this->current_index - 1); }
    void __set_warmup(size_t warmup_) { if (this->warmup < warmup_) this->warmup = warmup_; }
    void __set_in_exchange_view(bool x) { this->__in_exchange_view = x; }
    bool __is_valid_time(long long& datetime);
    long long __get_asset_time() const { return this->dt_index[this->current_index];}


    /// <summary>
    /// Does the asset's datetime index match the exchange's datetime index
    /// </summary>
    bool __is_aligned = false;

    /// <summary>
    /// Is the asset currently streaming //TODO: remove?
    /// </summary>
    bool __is_streaming = false;

    /// <summary>
    /// Has the asset finished streaming
    /// </summary>
    bool __is_expired = false;

    /// <summary>
    /// Will the asset be included in the exchange view
    /// </summary>
    bool __in_exchange_view = true;

    /// <summary>
    /// Is the current time step of the asset that last available time step.
    /// </summary>
    bool __is_valid_next_time = true;

protected:
    /// <summary>
    /// Load in a asset's data from a filepath. Supported types: csv, Parquet, HDF5.
    /// </summary>
    /// <param name="source">the file path of the data source</param>
    /// <param name="dt_fmt">the format of the datetime index</param>
    /// <param name="window">a range of valid times to load, in the form of seconds since midnight</param>
    /// <returns></returns>
        AGIS_API [[nodiscard]] AgisResult<bool> load(
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
        AGIS_API [[nodiscard]] AgisResult<bool> load(
            H5::DataSet& dataset,
            H5::DataSpace& dataspace,
            H5::DataSet& datasetIndex,
            H5::DataSpace& dataspaceIndex,
            std::string dt_fmt
        );
    #endif

    void __goto(long long datetime);
    void __reset();
    void __step();

    AGIS_API inline void __set_alignment(bool is_aligned_) { this->__is_aligned = is_aligned_; }
    bool __set_beta(AssetPtr market_asset, size_t lookback);
    void __set_volatility(size_t lookback);
    bool __set_beta(std::vector<double> beta_column);
    void __set_index(size_t index_) { this->asset_index = index_; }
    void __set_exchange_offset(size_t offset) { this->exchange_offset = offset; }

    /**
     * @brief is the asset a market asset, i.e. benchmark asset
    */
    bool __is_market_asset = false;

    bool __is_last_view() const { return this->current_index - 1 == this->rows; }
    bool __in_warmup() const { 
        if(this->current_index == 0) return true;
        return (this->current_index - 1) < this->warmup; 
    }


private:
    bool is_loaded = false;
    std::string asset_id;

    size_t asset_index;
    size_t exchange_offset;

    std::string exchange_id;
    std::string source;
    std::string dt_fmt;
    std::string tz;
    size_t warmup = 0;
    Frequency freq;

    size_t rows          = 0;
    size_t columns       = 0;
    size_t current_index = 0;
    size_t open_index;
    size_t close_index;
    long long* dt_index = nullptr;
    double* data        = nullptr;
    double* close       = nullptr;
    double* open        = nullptr;

    /**
     * @brief vector of rolling annualized volatility defined by some lookback N. Set via 
     * and exchange's set_volatility() method.
    */
    std::vector<double> volatility_vector;

    /**
     * @brief vector of rolling beta defined by some lookback N. Set via and exchanges set_market_asset
     * function call.
    */
    std::vector<double> beta_vector;

    /**
     * @brief mapping between observer's string rep and pointer to it.
    */
    ankerl::unordered_dense::map<std::string, AssetObserver*> observers;

    std::optional<std::pair<long long, long long>> window = std::nullopt;

    ankerl::unordered_dense::map<std::string, size_t> headers;

    [[nodiscard]] AgisResult<bool> load_headers();
    [[nodiscard]] AgisResult<bool> load_csv();
    const arrow::Status load_parquet();

};


struct MarketAsset
{
    // constructor
    MarketAsset(AssetPtr asset_, std::optional<size_t> beta_lookback_ = std::nullopt) 
    : asset(asset_), beta_lookback(beta_lookback_) 
    {
        this->market_index = asset->get_asset_index();
        this->market_id = asset->get_asset_id();
    }

    // allow for nullptr creation to be filled in later when restoring an exchange
    MarketAsset(std::string asset_id, std::optional<size_t> beta_lookback_ = std::nullopt)
        : asset(nullptr), beta_lookback(beta_lookback_)
    {
        this->market_id = asset_id;
    }

    // equality operator
    bool operator==(const MarketAsset& other) const
    {
        auto a = this->beta_lookback.value_or(0) == other.beta_lookback.value_or(0);
		auto b = this->market_id == other.market_id;
        return a && b;
	}
   
    size_t                  market_index;
    std::string             market_id;
    AssetPtr                asset;
    std::optional<size_t>   beta_lookback;
};

// Function to convert a string to Frequency enum
AGIS_API Frequency string_to_freq(const std::string& str);
AGIS_API std::string freq_to_string(Frequency freq);