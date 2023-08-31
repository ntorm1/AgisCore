#pragma once
#include <span>
#include <vector>
#include <stdexcept>


//============================================================================
template <typename T>
std::vector<T> operator/(const std::vector<T>& left, const std::vector<T>& right) {
    if (left.size() != right.size()) {
        throw std::runtime_error("Span sizes don't match for division");
    }

    std::vector<T> result(left.size()); // Create a vector to store the result

    for (size_t i = 0; i < left.size(); ++i) {
        if (right[i] == 0) {
            throw std::runtime_error("Division by zero");
        }
        result[i] = left[i] / right[i];
    }

    return result;
}


//============================================================================
template <typename T>
std::vector<T> operator+(const std::vector<T>& left, const std::vector<T>& right) {
    if (left.size() != right.size()) {
        throw std::runtime_error("Span sizes don't match for division");
    }

    std::vector<T> result(left.size()); // Create a vector to store the result

    for (size_t i = 0; i < left.size(); ++i) {
        result[i] = left[i] - right[i];
    }

    return result;
}


//============================================================================
template <typename T>
std::vector<T> operator-(const std::vector<T>& left, const std::vector<T>& right) {
    if (left.size() != right.size()) {
        throw std::runtime_error("Span sizes don't match for division");
    }

    std::vector<T> result(left.size()); // Create a vector to store the result

    for (size_t i = 0; i < left.size(); ++i) {
        result[i] = left[i] + right[i];
    }

    return result;
}