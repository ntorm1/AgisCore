#include "pch.h"
#include <string>
#include <fstream>
#include <algorithm>

#include <arrow/io/file.h>
#include <arrow/ipc/api.h>
#include <parquet/arrow/reader.h>
#include <arrow/filesystem/localfs.h>

#include "Asset.h"


//============================================================================
Asset::Asset(
    std::string asset_id_,
    std::string exchange_id_,
    size_t warmup_,
    Frequency freq_,
    std::string time_zone_)
{
    this->current_index = 0;
    this->asset_id = asset_id_;
    this->exchange_id = exchange_id_;
    this->warmup = warmup_;
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
NexusStatusCode Asset::load(
    std::string source_,
    std::string dt_fmt_,
    std::optional<std::pair<long long, long long>> window_)
{
    if (!is_file(source_))
    {
        return NexusStatusCode::InvalidArgument;
    }
    this->source = source_;
    this->dt_fmt = dt_fmt_;
    this->window = window_;

    auto filetype = file_type(source);
    NexusStatusCode res;
    switch (filetype)
    {
    case FileType::CSV:
        res = this->load_csv();
        break;
    case FileType::PARQUET: {
        auto arrow_res = this->load_parquet();
        if (arrow_res.code() != arrow::StatusCode::OK) {
            throw std::runtime_error("file load failed");
        }
        res = NexusStatusCode::Ok;
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
        res = this->load(
            dataset,
            dataspace,
            datasetIndex,
            dataspaceIndex
        );
        break;
    }
    default:
        throw std::runtime_error("Not implemented");
    }

    if (res != NexusStatusCode::Ok) { return res; }

    this->close = this->data + (this->rows) * this->close_index;
    this->open = this->data + (this->rows) * this->open_index;
    return res;
}

AGIS_API NexusStatusCode Asset::load(
    H5::DataSet& dataset,
    H5::DataSpace& dataspace,
    H5::DataSet& datasetIndex,
    H5::DataSpace& dataspaceIndex
)
{
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
    if (this->load_headers() != NexusStatusCode::Ok)
    {
        return NexusStatusCode::InvalidColumns;
    }
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
    double* columnMajorData = (double*)malloc(rows * columns * sizeof(double));
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

    return NexusStatusCode::Ok;
}


//============================================================================
NexusStatusCode Asset::load_headers()
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
        return NexusStatusCode::InvalidArgument;
    }
    else
    {
        return NexusStatusCode::Ok;
    }
}


//============================================================================
NexusStatusCode Asset::load_csv()
{
    std::ifstream file(this->source);
    if (!file) {
        return NexusStatusCode::InvalidIO;
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
        return NexusStatusCode::InvalidIO;
    }
    if (this->load_headers() != NexusStatusCode::Ok)
    {
        return NexusStatusCode::InvalidColumns;
    }
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
    return NexusStatusCode::Ok;
}


//============================================================================
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

    if (this->load_headers() != NexusStatusCode::Ok)
    {
        NexusStatusCode::InvalidColumns;
    }

    return arrow::Status::OK();
}


//============================================================================
StridedPointer<double> const Asset::__get_column(std::string const& column_name) const
{
    auto col_offset = this->headers.at(column_name);
    return StridedPointer(this->data + (col_offset*this->rows), this->rows, 1);
}


//============================================================================
StridedPointer<long long> const Asset::__get_dt_index() const
{
    return StridedPointer(this->dt_index,this->rows,1);
}


//============================================================================
AGIS_API std::vector<std::string> Asset::__get_dt_index_str() const
{
    auto dt_index = this->__get_dt_index();
    std::vector<std::string> dt_index_str;
    for (auto const& epoch : dt_index)
    {
        dt_index_str.push_back(epoch_to_str(epoch, this->dt_fmt));
    }
    return dt_index_str;
}


//============================================================================
void Asset::__step()
{
    this->current_index++;
    this->open++;
    this->close++;
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
void Asset::__goto(long long datetime)
{
    //goto date is beyond the datetime index
    // search for datetime in the index
    for (int i = this->current_index; i < this->rows; i++)
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
    // move datetime index and data pointer back to start
    this->current_index = this->warmup;
    this->__is_expired = false;
    if(!__is_aligned) this->__is_streaming = false;
    this->close = this->data + (this->rows) * this->close_index;
    this->open = this->data + (this->rows) * this->open_index;
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
AGIS_API double Asset::get_asset_feature(std::string const& col, int index) const noexcept
{
    size_t col_offset = this->headers.at(col) * this->rows;
    size_t row_offset = this->current_index + index - 1;
    return *(this->data + row_offset + col_offset);
}


//============================================================================
double Asset::__get(std::string col, size_t row) const
{
    auto col_offset = this->headers.at(col) * this->rows;
    return *(this->data + row + col_offset);
}


//============================================================================
Frequency string_to_freq(const std::string& str)
 {
     if (str == "Tick") {
         return Frequency::Tick;
     }
     else if (str == "Min1") {
         return Frequency::Min1;
     }
     else if (str == "Min5") {
         return Frequency::Min5;
     }
     else if (str == "Min15") {
         return Frequency::Min15;
     }
     else if (str == "Min30") {
         return Frequency::Min30;
     }
     else if (str == "Hour1") {
         return Frequency::Hour1;
     }
     else if (str == "Hour4") {
         return Frequency::Hour4;
     }
     else if (str == "Day1") {
         return Frequency::Day1;
     }
     else {
         return Frequency::Tick;
     }
}

template class AGIS_API StridedPointer<long long>;
template class AGIS_API StridedPointer<double>;
template class AGIS_API AgisMatrix<double>;