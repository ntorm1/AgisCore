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

    auto inline get_value(){return this->value;}
    std::string inline get_exception(){
        AgisException excep = std::get<AgisException>(this->value);
        return excep.what();
    }

private:
    ValueType value;
};


template <typename T, typename U>
AgisResult<U> ExtractException(AgisResult<T>& result) {
    if (result.is_exception()) {
        std::variant<T, AgisException> value = result->get_value();
        AgisException excep = std::get<AgisException>(this->value);
        return AgisResult<U>(excep.what());
    }
    return AgisResult<U>(result.unwrap(false));
}


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

#define AGIS_EXTRACT_OR_UNWRAP(result, NewType) \
    ((result).is_exception() ? \
        return ExtractException<decltype(result)::ValueType, NewType>(result) : \
        (result).unwrap(false))