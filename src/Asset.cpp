#include "Asset.h"
#include "pch.h"
#include <string>
#include <fstream>
#include <algorithm>

#include "AgisRisk.h"
#include "AgisObservers.h"

#ifdef ARROW_API_H
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <arrow/ipc/api.h>
#include <parquet/arrow/reader.h>
#include <arrow/filesystem/localfs.h>
#endif

//============================================================================
Asset::Asset(
    std::string asset_id_,
    std::string exchange_id_,
    std::optional<size_t> warmup_,
    Frequency freq_,
    std::string time_zone_)
{
    this->current_index = 0;
    this->asset_id = asset_id_;
    this->exchange_id = exchange_id_;
    this->warmup = warmup_.value_or(0);
    this->freq = freq_;
    this->tz = time_zone_;
}


//============================================================================
Asset::~Asset()
{
    if (this->is_loaded)
    {
        delete[] this->data;
        delete[] this->dt_index;
    }
}


//============================================================================
AgisResult<bool> Asset::load(
    std::string source_,
    std::string dt_fmt_,
    std::optional<std::pair<long long, long long>> window_)
{
    if (!is_file(source_))
    {
        return AgisResult<bool>(AGIS_EXCEP("file does not exist"));
    }
    this->source = source_;
    this->dt_fmt = dt_fmt_;
    this->window = window_;

    auto filetype = file_type(source);
    switch (filetype)
    {
    case FileType::CSV:
        AGIS_DO_OR_RETURN(this->load_csv(), bool);
        break;
    case FileType::PARQUET: {
        auto arrow_res = this->load_parquet();
        if (arrow_res.code() != arrow::StatusCode::OK) {
            return AgisResult<bool>(AGIS_EXCEP("file load failed"));
        }
        break;
    }
    case FileType::HDF5: {
        H5::H5File file(this->source, H5F_ACC_RDONLY);
        int numObjects = file.getNumObjs();
        std::string asset_id = file.getObjnameByIdx(0);
        H5::DataSet dataset = file.openDataSet(asset_id + "/data");
        H5::DataSpace dataspace = dataset.getSpace();
        H5::DataSet datasetIndex = file.openDataSet(asset_id + "/datetime");
        H5::DataSpace dataspaceIndex = datasetIndex.getSpace();
        AGIS_DO_OR_RETURN(this->load(
            dataset,
            dataspace,
            datasetIndex,
            dataspaceIndex,
            this->dt_fmt
        ), bool);
        break;
    }
    case FileType::UNSUPPORTED: {
        return AgisResult<bool>(AGIS_EXCEP("file type not supported"));
    }
    default:
        return AgisResult<bool>(AGIS_EXCEP("file type not supported"));
    }

    this->close = this->data + (this->rows) * this->close_index;
    this->open = this->data + (this->rows) * this->open_index;
    return AgisResult<bool>(true);
}


//============================================================================
#ifdef H5_HAVE_H5CPP
AGIS_API AgisResult<bool> Asset::load(
    H5::DataSet& dataset,
    H5::DataSpace& dataspace,
    H5::DataSet& datasetIndex,
    H5::DataSpace& dataspaceIndex,
    std::string dt_fmt_
)
{
    this->dt_fmt = dt_fmt_;

    // Get the number of attributes associated with the dataset
    int numAttrs = dataset.getNumAttrs();
    // Iterate through the attributes to find the column names
    for (int i = 0; i < numAttrs; i++) {
        // Get the attribute at index i
        H5::Attribute attr = dataset.openAttribute(i);

        // Check if the attribute is a string type
        if (attr.getDataType().getClass() == H5T_STRING) {
            // Read the attribute as a string
            std::string attrValue;
            attr.read(attr.getDataType(), attrValue);

            // Store the attribute value as a column name
            this->headers[attrValue] = static_cast<size_t>(i);
        }
    }
    
    AGIS_DO_OR_RETURN(this->load_headers(), bool);

    // Get the number of rows and columns from the dataspace
    int numDims = dataspace.getSimpleExtentNdims();
    std::vector<hsize_t> dims(numDims);
    dataspace.getSimpleExtentDims(dims.data(), nullptr);
    this->rows = dims[0];
    this->columns = dims[1];

    // Allocate memory for the column-major array
    this->data = new double[this->rows * this->columns];

    // Read the dataset into the column-major array
    //hsize_t memDims[2] = { this->columns, this->rows }; // Swap rows and columns for column-major
    //H5::DataSpace memspace(2, memDims);
    dataset.read(this->data, H5::PredType::NATIVE_DOUBLE, dataspace);

    // HDF5 stored in row major. Swap elements to get to col major. Maybe better way to do this
    double* columnMajorData = new double[rows * columns];
    // Copy elements from the row-major array to the column-major array
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < columns; ++j) {
            columnMajorData[j * rows + i] = data[i * columns + j];
        }
    }
    std::swap(this->data, columnMajorData);
    delete columnMajorData;

    // Allocate memory for the array to hold the data
    this->dt_index = new long long[this->rows];

    // Read the 1D datetime index from the dataset
    datasetIndex.read(this->dt_index, H5::PredType::NATIVE_INT64, dataspaceIndex);

    this->close = this->data + (this->rows) * this->close_index;
    this->open = this->data + (this->rows) * this->open_index;
    this->is_loaded = true;
    return AgisResult<bool>(true);
}
#endif


//============================================================================
AgisResult<bool> Asset::encloses(AssetPtr asset_b)
{
    if (!this->is_loaded) return AgisResult<bool>(false);
    if (this->rows < asset_b->rows) return AgisResult<bool>(false);

    auto asset_b_index = asset_b->__get_dt_index(false);
    auto asset_b_start_index_res = this->encloses_index(asset_b);
    if (asset_b_start_index_res.is_exception()) {
        return AgisResult<bool>(asset_b_start_index_res.get_exception());
    }
    auto asset_b_start_index = asset_b_start_index_res.unwrap();
	// check if asset_b is contained in this
	for (size_t i = 0; i < asset_b->rows; i++)
	{
		if (this->dt_index[asset_b_start_index + i] != asset_b_index[i])
		{
			return AgisResult<bool>(false);
		}
	}
	return AgisResult<bool>(true);
}


//============================================================================
AgisResult<size_t> Asset::encloses_index(AssetPtr asset_b)
{
    auto asset_b_index = asset_b->__get_dt_index(false);
    auto asset_b_start = asset_b_index[0];

    // find the index location of asset_b_start in this->dt_index if it exists
    auto it = std::find(this->dt_index, this->dt_index + this->rows, asset_b_start);
    if (it == this->dt_index + this->rows) {
        return AgisResult<size_t>(AGIS_EXCEP("asset_b_start not found in this->dt_index"));
    }
    auto asset_b_start_index = static_cast<size_t>(std::distance(this->dt_index, it));
    return AgisResult<size_t>(asset_b_start_index);
}


//============================================================================
void Asset::add_observer(AssetObserver* observer)
{
    // add observer if it does not already exist
    auto str_rep = observer->str_rep();
    auto it = this->observers.find(str_rep);
    if (it == this->observers.end())
    {
		this->observers.emplace(std::move(str_rep), observer);
	}
    else 
    {
        (*it).second->set_touch(true);
    }
}


//============================================================================
void Asset::remove_observer(AssetObserver* observer)
{
    // remove observer if it exists
    auto str_rep = observer->str_rep();
    if (this->observers.contains(str_rep))
    {
        this->observers.erase(str_rep);
    }
}


//============================================================================
std::vector<double> Asset::generate_baseline_returns(double starting_amount)
{
    std::vector<double> returns(this->rows, 0.0);
    auto close_price = this->__get_column(this->close_index);
    returns[0] = starting_amount / close_price[0];
    for (size_t i = 1; i < this->rows; i++)
    {
		returns[i] = returns[i - 1] * close_price[i] / close_price[i - 1];
	}
    return returns;
}

//============================================================================
AgisResult<bool> Asset::load_headers()
{
    int success = 0;
    for (const auto& pair : this->headers)
    {
        if (str_ins_cmp(pair.first, "Close"))
        {
            this->close_index = pair.second;
            success++;
        }
        else if (str_ins_cmp(pair.first, "Open"))
        {
            this->open_index = pair.second;
            success++;
        }
    }
    if (success != 2)
    {
        return AgisResult<bool>(AGIS_EXCEP("failed to find open and close columns"));
    }
    return AgisResult<bool>(true);
}


//============================================================================
AgisResult<bool> Asset::load_csv()
{
    std::ifstream file(this->source);
    if (!file) {
        return AgisResult<bool>(AGIS_EXCEP("invalid source file"));
    }

    this->rows = 0;
    std::string line;
    while (std::getline(file, line)) {
        this->rows++;
    }
    this->rows--;
    file.clear();                 // Clear any error flags
    file.seekg(0, std::ios::beg);  // Move the file pointer back to the start


    // Parse headers
    if (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string columnName;
        int columnIndex = 0;

        // Skip the first column (date)
        std::getline(ss, columnName, ',');
        while (std::getline(ss, columnName, ',')) {
            this->headers[columnName] = columnIndex;
            columnIndex++;
        }
    }
    else {
        return AgisResult<bool>(AGIS_EXCEP("failed to parse headers"));
    }
    AGIS_DO_OR_RETURN(this->load_headers(), bool);
    this->columns = this->headers.size();

    // Load in the actual data
    this->data = new double[this->rows*this->columns];
    this->dt_index = new long long[this->rows];

    size_t row_counter = 0;
    while (std::getline(file, line))
    {
        std::stringstream ss(line);

        // First column is datetime
        std::string dateStr, columnValue;
        std::getline(ss, dateStr, ',');

        auto datetime = str_to_epoch(dateStr, this->dt_fmt);

        // check to see if the datetime is in window
        this->dt_index[row_counter] = datetime;

        int col_idx = 0;
        while (std::getline(ss, columnValue, ','))
        {
            double value = std::stod(columnValue);
            this->data[row_counter + col_idx * this->rows] = value;
            col_idx++;
        }
        row_counter++;
    }
    this->is_loaded = true;
    return AgisResult<bool>(true);
}


//============================================================================
#ifdef ARROW_API_H
const arrow::Status Asset::load_parquet()
{
    arrow::SetCpuThreadPoolCapacity(1);
    arrow::io::SetIOThreadPoolCapacity(1);
    arrow::MemoryPool* pool = arrow::default_memory_pool();

    // Bind our input file to source
    std::shared_ptr<arrow::io::ReadableFile> infile;
    auto c = this->source.c_str();
    ARROW_ASSIGN_OR_RAISE(infile, arrow::io::ReadableFile::Open(c));

    // build parquet reader
    std::unique_ptr<parquet::arrow::FileReader> reader;
    PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(infile, pool, &reader));

    // Read entire file as a single Arrow table
    std::shared_ptr<arrow::Table> table;
    PARQUET_THROW_NOT_OK(reader->ReadTable(&table));

    this->rows = table->num_rows();
    this->columns = table->num_columns();

    this->data = new double[this->rows * this->columns];
    this->dt_index = new long long[this->rows];

    // get datetime index
    std::shared_ptr<arrow::Int64Array> index = std::static_pointer_cast<arrow::Int64Array>(table->column(0)->chunk(0));
    const int64_t* raw_index = index->raw_values();
    for (size_t i = 0; i < this->rows; i++) {
        this->dt_index[i] = raw_index[i];
    }

    // load in the datetime index
    std::shared_ptr<arrow::ChunkedArray> column = table->column(0);
    int64_t index_loc = 0;
    for (int chunk_index = 0; chunk_index < column->num_chunks(); chunk_index++) {
        std::shared_ptr<arrow::Array> chunk = column->chunk(chunk_index);

        // Check if the chunk is of type arrow::Int64Array (assuming the first column is of type int64)
        if (chunk->type()->id() == arrow::Type::INT64) {
            // Access the underlying data as a pointer to int64_t (long long)
            std::shared_ptr<arrow::Int64Array> int64_column = std::static_pointer_cast<arrow::Int64Array>(chunk);
            const int64_t* data_ptr = int64_column->raw_values();
            for (int64_t i = index_loc; i < int64_column->length(); i++) {
                this->dt_index[i] = raw_index[i];
            }
            index_loc += int64_column->length();
        }
    }
    // Loop through each column
    for (int col = 1; col < columns; col++) {
        std::shared_ptr<arrow::ChunkedArray> column = table->column(col);
        int64_t index_loc = this->rows * col;
        // Loop through each chunk of the column
        for (int chunk_index = 0; chunk_index < column->num_chunks(); chunk_index++) {
            std::shared_ptr<arrow::Array> chunk = column->chunk(chunk_index);

            // Check if the chunk is of type arrow::Int64Array (assuming the first column is of type int64)
            std::shared_ptr<arrow::DoubleArray> double_column = std::static_pointer_cast<arrow::DoubleArray>(table->column(0)->chunk(0));

            // Access the underlying data as a pointer to int64_t (long long)
            const double* raw_data = double_column->raw_values();
            for (int64_t i = 0; i < double_column->length(); i++) {
                this->data[i + index_loc] = raw_data[i];
            }
        }
    }

    // Get the column names from the schema
    std::shared_ptr<arrow::Schema> schema = table->schema();
    std::vector<std::string> column_names = schema->field_names();
    for (size_t i = 1; i < column_names.size(); i++) {
        this->headers[column_names[i]] = i;
    }

    if (this->load_headers().is_exception())
    {
        throw std::runtime_error("failed to load headers");
    }
    this->is_loaded = true;
    return arrow::Status::OK();
}
#endif

std::span<double> const Asset::__get_column(size_t column_index) const
{
    return std::span<double>(this->data + (column_index * this->rows), this->rows);
}

//============================================================================
std::span<double> const Asset::__get_column(std::string const& column_name) const
{
    auto col_offset = this->headers.at(column_name);
    return std::span<double>(this->data + (col_offset*this->rows), this->rows);
}


//============================================================================
std::span<long long> const Asset::__get_dt_index(bool adjust_for_warmup) const
{
    // return the dt index without the warmup period moving the pointer 
    // forward so that the final union index will be adjusted to all warmups
    if(adjust_for_warmup)
        return std::span(this->dt_index + this->warmup,this->rows - this->warmup);
    else
        return std::span(this->dt_index, this->rows);
}


//============================================================================
std::vector<std::string> Asset::__get_dt_index_str(bool adjust_for_warmup) const
{
    auto dt_index = this->__get_dt_index(adjust_for_warmup);
    std::vector<std::string> dt_index_str;
    for (auto const& epoch : dt_index)
    {
        dt_index_str.push_back(epoch_to_str(epoch, this->dt_fmt).unwrap());
    }
    return dt_index_str;
}


//============================================================================
bool Asset::__set_beta(AssetPtr market_asset, size_t lookback)
{
    auto market_close_col_index = market_asset->__get_close_index();
    std::span<double> market_close_col = market_asset->__get_column(market_close_col_index);
    std::span<double> close_column = this->__get_column(this->close_index);

    if (lookback >= close_column.size())
    {
        return false;
    }   

    // adjust the warmup to account for the lookback period
    this->__set_warmup(lookback);
    
    std::span<long long> market_datetime_index = market_asset->__get_dt_index(false);
    std::span<long long> datetime_index = this->__get_dt_index(false);
    long long first_datetime = datetime_index[0];
    
    // find the first datetime in the market asset that is equal to the first datetime in this asset
    auto first_datetime_index = std::find(
        market_datetime_index.begin(),
        market_datetime_index.end(),
        first_datetime
    );
    // get this index location
    auto first_datetime_index_loc = std::distance(
        market_datetime_index.begin(),
        first_datetime_index
    );
 
    std::vector<double> returns_this, returns_market;
    returns_this.resize(this->rows - 1);
    returns_market.resize(this->rows - 1);

    // Calculate the daily returns for both this asset and the market asset
    for (size_t i = 1; i < this->rows; i++)
    {
        double return_this = (close_column[i] - close_column[i - 1]) / close_column[i - 1];
        double return_market = (market_close_col[i + first_datetime_index_loc] - market_close_col[i + first_datetime_index_loc - 1]) /
            market_close_col[i + first_datetime_index_loc - 1];

        returns_this[i-1] = return_this;
        returns_market[i-1] = return_market;
    }
    this->beta_vector = rolling_beta(returns_this, returns_market, lookback);
    assert(this->beta_vector.size() == this->rows);
    return true;
}


//============================================================================
void Asset::__set_volatility(size_t lookback)
{
    // adjust the warmup to account for the lookback period
    this->__set_warmup(lookback);
    // calculate volatility using closing prices
    auto close_span = this->__get_column(this->close_index);
    this->volatility_vector = rolling_volatility(close_span, lookback);
    assert(this->volatility_vector.size() == this->rows);
}


//============================================================================
bool Asset::__set_beta(std::vector<double> beta_column)
{
    // allow for explcit setting of beta instead of rolling cov/var. Also used to
    // set the market asset beta column to 1
    this->beta_vector = beta_column;
    return true;
}


//============================================================================
AGIS_API AgisResult<double> Asset::get_volatility() const
{
    if (this->volatility_vector.size() && !this->__in_warmup())
    {
        return AgisResult<double>(this->volatility_vector[this->current_index - 1]);
    }
    else
    {
        return AgisResult<double>(AGIS_EXCEP("volatility not available"));
    }
}


//============================================================================
AGIS_API AgisResult<double> Asset::get_beta() const
{
    if (this->beta_vector.size() && !this->__in_warmup())
    {
        return AgisResult<double>(this->beta_vector[this->current_index - 1]);
    }
    else if (this->__is_market_asset) {
        return AgisResult<double>(1.0f);
    }
    else
    {
        return AgisResult<double>(AGIS_EXCEP("beta not available"));
    }
}


//============================================================================
AGIS_API const std::span<double const> Asset::get_beta_column() const
{
    return std::span<double const>(this->beta_vector);
}


//============================================================================
AGIS_API const std::span<double const> Asset::get_volatility_column() const
{
    return std::span<double const>(this->volatility_vector);
}


//============================================================================
bool Asset::__is_valid_time(long long& datetime)
{
    if (!this->window) { return true; }

    std::pair<long long, long long > w = this->window.value();
    auto seconds_since_midnight = datetime % (24 * 60 * 60);

    // Extract the fractional part of the timestamp (nanoseconds)
    long long fractional_part = datetime % 1000000000;
    auto t = seconds_since_midnight * 1000000000LL + fractional_part;

    if (t < w.first || t > w.second) { return false; }
    return true;
}


//============================================================================
void Asset::__step()
{
    this->current_index++;
    this->open++;
    this->close++;
    if (this->__in_warmup()) this->__is_streaming = false;
    else this->__is_streaming = true;

    if (this->observers.size()) {
        for (auto& observer : observers) {
            observer.second->on_step();
        }
    }
}


//============================================================================
void Asset::__goto(long long datetime)
{
    //goto date is beyond the datetime index
    // search for datetime in the index
    for (size_t i = this->current_index; i < this->rows; i++)
    {
        if (this->__get_asset_time() >= datetime)
        {
            return;
        }
        this->__step();
    }
}


//============================================================================
void Asset::__reset()
{
    if (this->observers.size()) {
        for (auto& observer : observers) {
            observer.second->on_reset();
        }
    }

    // move datetime index and data pointer back to start
    this->current_index = 0;
    this->__is_expired = false;
    if(!__is_aligned) this->__is_streaming = false;
    this->close = (this->data + (this->rows) * this->close_index);
    this->open = (this->data + (this->rows) * this->open_index);

    // step forward untill warmup is reached
    for (int i = 0; i < this->warmup; i++)
    {
        this->__step();
    }
}


//============================================================================
AGIS_API double Asset::__get_market_price(bool on_close) const
{
    if (on_close) return *(this->close - 1);
    else return *(this->open - 1);
}


//============================================================================
AgisMatrix<double> const Asset::__get__data() const
{
    return AgisMatrix(this->data, this->rows, this->columns);
}


AGIS_API size_t Asset::get_current_index() const
{
    if (this->current_index == 0) return 0;
    return this->current_index - 1;
}

//============================================================================
AGIS_API std::vector<std::string> Asset::get_column_names() const
{
    std::vector<std::string> keys;
    for (const auto& pair : this->headers) {
        keys.push_back(pair.first);
    }
    return keys;
}


//============================================================================
AgisResult<double> Asset::get_asset_feature(std::string const& col, int index) const
{
#ifdef _DEBUG

    if (abs(index) > static_cast<int>(current_index - 1) || index > 0) 
    {
        return AgisResult<double>(AGIS_EXCEP("Invalid row index: " + std::to_string(index)));
    }
    if (!__is_streaming)
    {
		return AgisResult<double>(AGIS_EXCEP("Asset is not streaming"));
	}
    if(!this->headers.contains(col)) 
	{
		return AgisResult<double>(AGIS_EXCEP("Column does not exist: " + col));
	}
#endif

    size_t col_offset = this->headers.at(col) * this->rows;
    size_t row_offset = this->current_index + index - 1;
    return AgisResult<double> (*(this->data + row_offset + col_offset));
}


//============================================================================
AgisResult<double> Asset::get_asset_feature(size_t col, int index) const
{
#ifdef _DEBUG
    if (abs(index) > static_cast<int>(current_index - 1) || index > 0)
    {
        return AgisResult<double>(AGIS_EXCEP("Invalid row index: " + std::to_string(index)));
    }
    if (!__is_streaming)
    {
        return AgisResult<double>(AGIS_EXCEP("Asset is not streaming"));
    }
#endif

    size_t col_offset = col * this->rows;
    size_t row_offset = this->current_index + index - 1;
    return AgisResult<double>(*(this->data + row_offset + col_offset));
}


//============================================================================
AgisResult<double> Asset::get_asset_observer_result(std::string const& observer_name) const noexcept
{
    auto it = this->observers.find(observer_name);
	if (it == this->observers.end())
	{
		return AgisResult<double>(AGIS_EXCEP("Observer does not exist: " + observer_name));
	}
    return AgisResult<double>((*it).second->get_result());
}


//============================================================================
AgisResult<AssetObserver*> Asset::get_observer(std::string const& id) const noexcept
{
    auto it = this->observers.find(id);
    if (it == this->observers.end())
    {
        return AgisResult<AssetObserver*>(AGIS_EXCEP("Observer does not exist: " + id));
    }
    return AgisResult<AssetObserver*>((*it).second);
}


//============================================================================
void Asset::assign_asset_feature(size_t col, int index, AgisResult<double>& res)
{
#ifdef _DEBUG
    if (abs(index) > static_cast<int>(current_index - 1) || index > 0)
    {
        res.set_excep(AGIS_EXCEP("Invalid row index: " + std::to_string(index)));
    }
    if (!__is_streaming)
    {
        res.set_excep(AGIS_EXCEP("Asset is not streaming: " + std::to_string(index)));
    }
#endif
    size_t col_offset = col * this->rows;
    size_t row_offset = this->current_index + index - 1;
    res.set_value(*(this->data + row_offset + col_offset));
}


//============================================================================
double Asset::__get(std::string col, size_t row) const
{
    auto col_offset = this->headers.at(col) * this->rows;
    return *(this->data + row + col_offset);
}

template class AGIS_API StridedPointer<long long>;
template class AGIS_API StridedPointer<double>;
template class AGIS_API AgisMatrix<double>;
