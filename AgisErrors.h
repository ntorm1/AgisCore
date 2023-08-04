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


#include <exception>
#include <string>

class AgisException : public std::exception {
private:
    std::string message;

public:
    AgisException(const std::string& msg) : message(msg) {}

    // Override the what() method to provide a custom error message
    const char* what() const noexcept override {
        return message.c_str();
    }
};

#define AGIS_THROW(msg) \
    throw AgisException(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + msg)

#define AGIS_TRY(action) \
    try { \
        action; \
    } catch (const std::exception& e) { \
        throw AgisException(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + e.what()); \
    }