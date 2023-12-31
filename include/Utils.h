#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"
#include "AgisErrors.h"
#include <filesystem>
#include <string>

namespace fs = std::filesystem;


enum class FileType
{
	CSV,
	PARQUET,
	HDF5,
	UNSUPPORTED
};

AGIS_API bool str_ins_cmp(const std::string& s1, const std::string& s2);

AGIS_API long long str_to_epoch(
	const std::string& dateString,
	const std::string& formatString);

AGIS_API AgisResult<std::string> epoch_to_str(
	long long epochTime,
	const std::string& formatString);

AGIS_API std::vector<std::string> files_in_folder(const std::string& folderPath);
AGIS_API bool is_folder(const std::string& path);
AGIS_API bool is_file(const std::string& path);
AGIS_API bool is_valid_class_name(const std::string& name);
AGIS_API FileType file_type(const std::string& path);
AGIS_API std::string join_paths(const std::string& parentPath, const std::string& childPath);