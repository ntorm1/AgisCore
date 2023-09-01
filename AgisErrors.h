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
struct AGIS_API AgisResult {
public:
    // Define the variant type with T and AgisException
    using ValueType = std::variant<T, AgisException>;
    inline AgisResult() : value(T()) {}
    inline AgisResult(ValueType&& _value) : value(std::move(_value)) {
        this->is_value_exception = std::holds_alternative<AgisException>(this->value);
    }

    inline bool is_exception()
    {
        return this->is_value_exception;
    }

    inline T unwrap(bool panic = true)
    {
        if (std::holds_alternative<T>(this->value))
        {
            return std::move(std::get<T>(this->value));
        }
        if (panic)
        {
            throw std::get<AgisException>(this->value);
        }
        return T();
    }

    inline T unwrap_or(T val)
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
    bool is_value_exception = false;
    ValueType value;
};


template <typename T, typename U>
AgisResult<U> ExtractException(AgisResult<T>& result) {
    if (result.is_exception()) 
    {
        std::variant<T, AgisException> value = result.get_value();
        AgisException excep = std::get<AgisException>(value);
        return AgisResult<U>(excep.what());
    }
    else
    {
        throw std::runtime_error("Cannot extract exception from non-exception result");
    }
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

#define AGIS_TRY_RESULT(functionCall, AgisResultType) \
    [&]() -> AgisResult<AgisResultType> { \
        try { \
            functionCall; \
        } catch (const std::exception &ex) { \
            return AgisResult<AgisResultType>(AGIS_EXCEP(std::string("Exception caught: ") + ex.what())); \
        } \
    }()

#define AGIS_DO_OR_RETURN(functionCall, AgisResultType) \
    do { \
        AgisResult<AgisResultType> res = functionCall; \
        if (res.is_exception()) { \
            return res;\
        } \
    } while (false)

#define AGIS_DO_OR_THROW(functionCall) \
    do { \
        auto res = functionCall; \
        if (res.is_exception()) { \
            throw res.get_exception();\
        } \
    } while (false)


#define AGIS_ASSIGN_OR_RETURN(assignment, result, exceptionType, targetType) \
    do { \
        auto _result = (result); \
        if (_result.is_exception()) { \
            return ExtractException<exceptionType, targetType>(_result); \
        } else { \
            targetType _value = _result.unwrap(); \
            assignment = _value; \
        } \
    } while (false)

#define AGIS_EXTRACT_OR_UNWRAP(result, NewType) \
    ((result).is_exception() ? \
        return ExtractException<decltype(result)::ValueType, NewType>(result) : \
        (result).unwrap(false))