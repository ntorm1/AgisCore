#include "pch.h"
#include <string>
#include <fstream>

#include "Asset.h"

Asset::Asset(
    std::string asset_id_,
    std::string exchange_id_,
    size_t warmup_,
    Frequency freq_,
    std::string time_zone_)
{
    this->asset_id = asset_id_;
    this->exchange_id = exchange_id_;
    this->warmup = warmup_;
    this->freq = freq_;
    this->tz = time_zone_;
}

Asset::~Asset()
{
    if (this->is_loaded)
    {
        delete[] this->data;
        delete[] this->dt_index;
    }
}

NexusStatusCode Asset::load(
    std::string source_,
    std::string dt_fmt_)
{
    if (!is_file(source_))
    {
        return NexusStatusCode::InvalidArgument;
    }
    this->source = source_;
    this->dt_fmt = dt_fmt_;
    if (is_csv(source))
    {
        return this->load_csv();
    }
    else
    {
        throw std::runtime_error("Not implemented");
    }
}

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

NexusStatusCode Asset::load_csv()
{
    std::ifstream file(this->source);
    if (!file) {
        return NexusStatusCode::InvalidIO;
    }

    // Parse headers
    std::string line;
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
        NexusStatusCode::InvalidColumns;
    }
    this->columns = this->headers.size();

    // Load in the actual data
    unsigned int capacity = 1000;
    this->data = new double[capacity];
    this->dt_index = new long long[capacity];
    while (std::getline(file, line))
    {
        std::stringstream ss(line);

        // First column is datetime
        std::string dateStr, columnValue;
        std::getline(ss, dateStr, ',');

        
        if ((this->rows + 1) * this->columns >= capacity)
        {
            capacity *= 2;
            double* newArray = static_cast<double*>(realloc(this->data, capacity * sizeof(double)));
            long long* newDtArray = static_cast<long long*>(realloc(this->dt_index, capacity * sizeof(long long)));
            if (!newArray || !newDtArray) {
                return NexusStatusCode::InvalidMemoryOp;
            }
            this->data = newArray;
            this->dt_index = newDtArray;
        }

        this->dt_index[this->rows] = str_to_epoch(dateStr, this->dt_fmt);

        int col_idx = 0;
        while (std::getline(ss, columnValue, ','))
        {
            double value = std::stod(columnValue);
            this->data[this->rows*this->columns + col_idx] = value;
            col_idx++;
        }
        this->rows++;
    }

    double* newArray = static_cast<double*>(realloc(
        this->data,
        this->rows*this->columns * sizeof(double)));
    long long* newDtArray = static_cast<long long*>(realloc(
        this->dt_index,
        this->rows * this->columns * sizeof(long long)));
    if (!newArray || !newDtArray) {
        return NexusStatusCode::InvalidMemoryOp;
    }
    this->data = newArray;
    this->dt_index = newDtArray;
    this->row = this->data;
    this->is_loaded = true;
    return NexusStatusCode::Ok;
}


StridedPointer<double> const Asset::__get_column(std::string const& column_name) const
{
    auto col_offset = this->headers.at(column_name);
    return StridedPointer(this->data + col_offset, this->rows, this->columns);
}

StridedPointer<long long> const Asset::__get_dt_index() const
{
    return StridedPointer(this->dt_index,this->rows,1);
}

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

void Asset::__step()
{
    this->row += this->columns;
    this->current_index++;
}

void Asset::__goto(long long datetime)
{
    //goto date is beyond the datetime index
    if (datetime >= this->dt_index[this->rows - 1])
    {
        this->current_index = this->rows - 1;
        this->row = this->data + ((this->rows - 1) * this->columns);
    }
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

void Asset::__reset()
{
    // move datetime index and data pointer back to start
    this->current_index = this->warmup;
    this->row = &this->data[this->warmup * this->columns];
    this->__is_expired = false;
    if(!__is_aligned) this->__is_streaming = false;
}

AGIS_API double Asset::__get_market_price(bool on_close) const
{
    if (on_close)
    {
        return *(this->row  - this->columns + this->close_index);
    }
    else
    {
        return *(this->row -this->columns + this->open_index);
    }
}

AgisMatrix<double> const Asset::__get__data() const
{
    return AgisMatrix(this->data, this->rows, this->columns);
}

AGIS_API std::vector<std::string> Asset::get_column_names() const
{
    std::vector<std::string> keys;
    for (const auto& pair : this->headers) {
        keys.push_back(pair.first);
    }
    return keys;
}

double Asset::__get(std::string col, size_t row) const
{
    auto offset = this->columns * row;
    auto col_idx = this->headers.at(col);
    return *(this->data + offset + col_idx);
}

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