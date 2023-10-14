#pragma once

#include <exception>
#include <string>


enum class AgisErrorCode : uint16_t {
    OUT_OF_RANGE = 1,
    INVALID_ARGUMENT = 2,
    NOT_IMPLEMENTED = 3,
    INVALID_STATE = 4,
    INVALID_OPERATION = 5,
    INVALID_FORMAT = 6,
    INVALID_DATA = 7,
    INVALID_CONFIGURATION = 8,
    INVALID_ENVIRONMENT = 9,
    INVALID_PATH = 10,
};

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