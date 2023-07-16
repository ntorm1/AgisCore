#include "pch.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>
#include "Utils.h"

bool str_ins_cmp(const std::string& a, const std::string& b)
{
    return std::equal(a.begin(), a.end(),
        b.begin(), b.end(),
        [](char a, char b) {
            return tolower(a) == tolower(b);
        });
}

long long str_to_epoch(
    const std::string& dateString,
    const std::string& formatString)
{
    std::tm timeStruct = {};
    std::istringstream iss(dateString);
    iss >> std::get_time(&timeStruct, formatString.c_str());

    std::time_t utcTime = std::mktime(&timeStruct);

    // Convert to std::chrono::time_point
    std::chrono::system_clock::time_point timePoint = std::chrono::system_clock::from_time_t(utcTime);

    // Get the epoch time in nanoseconds
    return std::chrono::duration_cast<std::chrono::nanoseconds>(timePoint.time_since_epoch()).count();
}

std::string epoch_to_str(
    long long epochTime,
    const std::string& formatString)
{
    // Convert the epoch time from nanoseconds to seconds
    std::chrono::nanoseconds duration(epochTime);
    std::chrono::seconds epochSeconds = std::chrono::duration_cast<std::chrono::seconds>(duration);

    // Convert the epoch time to std::time_t
    std::time_t epoch = epochSeconds.count();

    // Convert std::time_t to std::tm
    std::tm timeStruct;
    gmtime_s(&timeStruct, &epoch); // Use gmtime_s for safer handling

    // Format the time struct to a string
    std::stringstream ss;
    ss << std::put_time(&timeStruct, formatString.c_str());
    return ss.str();
}

bool is_file(const std::string& filePath)
{
    return fs::exists(filePath) && fs::is_regular_file(filePath);
}

bool is_folder(const std::string& path)
{
    std::filesystem::path fsPath(path);
    return std::filesystem::is_directory(fsPath);
}

std::string join_paths(const std::string& parentPath, const std::string& childPath)
{
    fs::path fullPath = fs::path(parentPath) / fs::path(childPath);
    return fullPath.string();
}

FileType file_type(const std::string& filePath) {
    fs::path path(filePath);
    std::string extension = path.extension().string();
    if (extension == ".csv")        return FileType::CSV;
    if (extension == ".parquet")    return FileType::PARQUET;
    if (extension == ".h5")         return FileType::HDF5;
    throw std::runtime_error("not impl");
}

std::vector<std::string> files_in_folder(const std::string& folderPath)
{
    std::vector<std::string> files;

    for (const auto& entry : std::filesystem::directory_iterator(folderPath))
    {
        if (std::filesystem::is_regular_file(entry))
        {
            files.push_back(entry.path().string());
        }
    }

    return files;
}