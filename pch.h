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
#define USER_ANKERL
#define USE_LUAJIT

// add headers that you want to pre-compile here
#include "framework.h"
#include <memory>
#include <vector>
#include <optional>
#include <mutex>
#include <atomic>
#include <variant>
#include <ankerl/unordered_dense.h>
#include "utils_gmp.h"
#include "AgisPointers.h"
#include "AgisErrors.h"
#include "json.hpp"

#ifdef USE_LUAJIT
#define SOL_LUAJIT 1
#include <sol/sol.hpp>
#endif

typedef unsigned int uint;


using json = nlohmann::json;


constexpr auto DEFAULT_STRAT_ID = 0;


#endif //PCH_H
