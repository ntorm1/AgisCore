#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#define AGIS_SLOW

enum class AGIS_API NexusStatusCode {
	Ok,
	InvalidIO,
	InvalidArgument,
	InvalidId,
	InvalidMemoryOp,
	InvalidColumns,
	InvalidTz
};
