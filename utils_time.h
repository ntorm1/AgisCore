#pragma once
#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <utility>


extern AGIS_API const std::pair<long long, long long> UsEquityRegularHours = {
    (9 * 60 * 60 + 30 * 60) * 1000000000LL,
    16 * 60 * 60 * 1000000000LL
};