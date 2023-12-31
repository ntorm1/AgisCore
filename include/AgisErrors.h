#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

//#define AGIS_SLOW

#include <variant>

#include "AgisException.h"

enum class AGIS_API NexusStatusCode {
    Ok,
    InvalidIO,
    InvalidArgument,
    InvalidId,
    InvalidMemoryOp,
    InvalidColumns,
    InvalidTz
};


template <typename T>
struct AGIS_API AgisResult {
public:
    // Define the variant type with T and AgisException
    using ValueType = std::variant<T, AgisException>;
    inline AgisResult() : value(T()) {}
    
    // Constructor for values
    inline AgisResult(ValueType&& _value) : value(std::move(_value)) {
        this->is_value_exception = std::holds_alternative<AgisException>(this->value);
    }

    inline bool is_exception()
    {
        return this->is_value_exception;
    }

    // Check if the stored value is NaN (specialization for double)
    template <typename U = T>
    std::enable_if_t<std::is_same_v<U, double>, bool> is_nan() const {
        return std::isnan(std::get<double>(value));
    }

    // Provide an implementation for other types (e.g., return false)
    template <typename U = T>
    std::enable_if_t<!std::is_same_v<U, double>, bool> is_nan() const {
        return false;
    }

    inline T unwrap(bool panic = true)
    {
        if (!this->is_value_exception) [[likely]]
        {
            return std::move(std::get<T>(this->value));
        }
        if (panic)
        {
            throw std::get<AgisException>(this->value);
        }
        return T();
    }


    inline void set_value(T val)
	{
		this->value = val;
	}


    inline void set_excep(AgisException val)
    {
        this->value = val;
        this->is_value_exception = true;
    }


    inline T unwrap_or(T val)
    {
        if (!this->is_value_exception) [[likely]]
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


#define AGIS_FORWARD_EXCEP(msg) \
    AgisException(msg)

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

#define AGIS_DO(functionCall) \
    do { \
        auto res = functionCall; \
        if (res.is_exception()) { \
            return res;\
        } \
    } while (false)


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


#define AGIS_EXTRACT_OR_UNWRAP(result, NewType) \
    ((result).is_exception() ? \
        return ExtractException<decltype(result)::ValueType, NewType>(result) : \
        (result).unwrap(false))