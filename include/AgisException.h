#pragma once

#include <exception>
#include <string>


enum class AgisErrorCode : uint16_t {
    OUT_OF_RANGE = 0,
    INVALID_ARGUMENT = 1,
    NOT_IMPLEMENTED = 2,
    INVALID_STATE = 3,
    INVALID_OPERATION = 4,
    INVALID_FORMAT = 5,
    INVALID_DATA = 6,
    INVALID_CONFIGURATION = 7,
    INVALID_ENVIRONMENT = 8,
    INVALID_PATH = 9,
};

extern const char* AgisErrorCodeStrings[];

#define AGIS_NOT_IMPL return std::unexpected<AgisErrorCode>(AgisErrorCode::NOT_IMPLEMENTED);

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