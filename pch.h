// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.
#ifndef PCH_H
#define PCH_H
#define NOMINMAX 
#pragma warning(disable:4146)
#pragma warning(disable:4251)

#define ARROW_API_H
#define H5_HAVE_H5CPP
#define USE_TBB

// add headers that you want to pre-compile here
#include "framework.h"
#include <memory>
#include <vector>
#include <optional>
#include <mutex>
#include <atomic>
#include <variant>
#include <unordered_map>
 
#include "utils_gmp.h"

typedef unsigned int uint;


constexpr auto DEFAULT_STRAT_ID = 0;


#endif //PCH_H
