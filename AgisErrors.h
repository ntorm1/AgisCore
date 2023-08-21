#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#define AGIS_SLOW

#include <exception>
#include <string>
#include <variant>

enum class AGIS_API NexusStatusCode {
    Ok,
    InvalidIO,
    InvalidArgument,
    InvalidId,
    InvalidMemoryOp,
    InvalidColumns,
    InvalidTz
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

template <typename T>
class AgisResult {
public:
    // Define the variant type with T and AgisException
    using ValueType = std::variant<T, AgisException>;

    AGIS_API inline AgisResult(const ValueType& _value)
    {
        this->value = _value;
    }

    AGIS_API inline bool is_exception()
    {
        return std::holds_alternative<AgisException>(this->value);
    }

    AGIS_API inline T unwrap(bool panic = true)
    {
        if (!this->is_exception()) 
        {
            return std::get<T>(this->value);
        }
        if (panic)
        {
            throw std::get<AgisException>(this->value);
        }
        return T();
    }

    AGIS_API inline T unwrap_or(T val)
    {
        if (!this->is_exception()) 
        {
            return std::get<T>(this->value);
        }
        else
        {
            return val;
        }
    }

private:
    ValueType value;
};

#define AGIS_EXCEP(msg) \
    AgisException(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + msg)

#define AGIS_THROW(msg) \
    throw AgisException(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + msg)

#define AGIS_TRY(action) \
    try { \
        action; \
    } catch (const std::exception& e) { \
        throw AgisException(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + e.what()); \
    }

#define AGIS_UNWRAP(result, variable_name) \
    T variable_name = result.unwrap(false)