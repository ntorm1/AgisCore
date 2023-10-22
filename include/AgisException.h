#pragma once

#include <exception>
#include <string>


enum class AgisStatusCode : uint16_t {
    OK = 0,
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

extern const char* AgisErrorCodeStrings[];

#define AGIS_NOT_IMPL return std::unexpected<AgisStatusCode>(AgisStatusCode::NOT_IMPLEMENTED);

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


#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b

#define JSON_GET_OR_CONTINUE(val, function, key) \
    if(!function.HasMember(key)) { \
		continue; \
	} \
    auto const& val = function[key];

#define AGIS_ASSIGN_OR_RETURN(val, function) \
	auto CONCAT(val, _opt) = function; \
    if (!CONCAT(val, _opt)) { \
		return std::unexpected<AgisException>(CONCAT(val, _opt).error()); \
	} \
	auto val = std::move(CONCAT(val, _opt).value());