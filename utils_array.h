#pragma once


#include "pch.h"
#include <queue>
#include <span>
#include <type_traits>
#include <stdexcept>
#include <optional>

using namespace std;


template<typename T, typename Func, typename I>
inline optional<T> unsorted_vector_remove(vector<T>& vec, Func func, I id) {
    static_assert(std::is_invocable_r<I, Func, T>::value, "Func must take parameter of type T and return type I.");

    size_t index = 0;
    bool found_item = false;
    for (const auto& item : vec) {
        if (func(item) == id)
        {
            found_item = true;
            break;
        }
        else {
            index++;
        }
    }
    if (!found_item)
    {
        return nullopt;
    }
    else
    {
        std::swap(vec[index], vec.back());
    }
    auto order = std::move(vec.back());
    vec.pop_back();
    return order;
}
template <typename T>
std::unique_ptr<T> unsorted_unique_ptr_remove(std::vector<std::unique_ptr<T>>& vec, size_t index) {
    // Move the unique pointer to a local variable
    std::unique_ptr<T> removedPtr = std::move(vec[index]);

    // Swap the element to be removed with the last element
    std::swap(vec[index], vec.back());
    // Pop the last element (which was the element to be removed)
    vec.pop_back();

    // Return the removed unique pointer
    return removedPtr;
}

template<typename T, typename Func>
inline optional<T> sorted_vector_remove(vector<T>& vec, Func func)
{
    // func must take type T and return bool as to wether it is the item to remove
    static_assert(std::is_invocable_r<bool, Func, T>::value, "Func must take parameter of type T and return type bool.");

    auto it = std::find_if(
        vec.begin(),
        vec.end(),
        func);

    if (it == vec.end())
    {
        // item is not found
        return nullopt;
    }

    // move item out of the vector then erase the iterator 
    auto item = std::move(*it);
    vec.erase(it);
    return item;
}

template<class T>
bool array_eq(T const* a, T const* b, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

template<class T>
tuple<T*, int> sorted_union(T const* p1, T const* p2, size_t n, size_t m) {
    int i = 0, j = 0, k = 0;
    auto* result = new T[n + m];
    priority_queue<T, vector<T>, greater<>> pq;

    while (i < n && j < m) {
        if (p1[i] < p2[j]) {
            pq.push(p1[i]);
            i++;
        }
        else if (p2[j] < p1[i]) {
            pq.push(p2[j]);
            j++;
        }
        else {
            pq.push(p1[i]);
            i++;
            j++;
        }
    }

    while (i < n) {
        pq.push(p1[i]);
        i++;
    }

    while (j < m) {
        pq.push(p2[j]);
        j++;
    }

    while (!pq.empty()) {
        result[k] = pq.top();
        pq.pop();
        k++;
    }

    // Shrink the memory block to the actual size needed
    auto* temp = (T*)realloc(result, k * sizeof(T));
    if (temp != nullptr) {
        result = temp;
    }
    else {
        throw std::runtime_error("failed to realloc array");
    }
    return std::make_tuple(result, k);
}

/**
 * Returns a sorted array of each each element's child array
 *
 * @param hash_map the container holding the elements to iterate over
 * @return pointer to dynamically allocated array
 *
 * @tparam Container The type of the container. It must support iteration over its values.
 * @tparam IndexLoc function to call on container elements to get array location
 * @tparam IndexLen function to call on container elements to get array length
 */
template<typename Container, typename IndexLoc, typename IndexLen>
tuple<long long*, int> inline container_sorted_union(
    Container& hash_map,
    IndexLoc index_loc,
    IndexLen index_len) {
    //allocate location for new sorted array
    auto* sorted_array = new long long[0];
    size_t length = 0;

    for (const auto& it : hash_map) {
        auto element = it.second;

        if (length == index_len(element)) {
            if (array_eq(sorted_array, index_loc(element), length)) {
                continue;
            }
        }
        //get sorted union of the two datetime indecies
        auto sorted_index_tuple = sorted_union(
            sorted_array, index_loc(element),
            length, index_len(element));

        auto sorted_index = get<0>(sorted_index_tuple);
        auto sorted_index_size = get<1>(sorted_index_tuple);

        //swap pointers between the new sorted union and the existing one
        std::swap(sorted_array, sorted_index);
        length = sorted_index_size;

        delete[] sorted_index;
    }
    return std::make_tuple(sorted_array, length);
}

template<typename Container, typename IndexLoc, typename IndexLen>
tuple<long long*, int> inline vector_sorted_union(
    Container& vec,
    IndexLoc index_loc,
    IndexLen index_len) {
    //allocate location for new sorted array
    auto* sorted_array = new long long[0];
    size_t length = 0;

    for (const auto& element : vec) {

        if (length == index_len(element)) {
            if (array_eq(sorted_array, index_loc(element), length)) {
                continue;
            }
        }
        //get sorted union of the two datetime indecies
        auto sorted_index_tuple = sorted_union(
            sorted_array, index_loc(element),
            length, index_len(element));

        auto sorted_index = get<0>(sorted_index_tuple);
        auto sorted_index_size = get<1>(sorted_index_tuple);

        //swap pointers between the new sorted union and the existing one
        std::swap(sorted_array, sorted_index);
        length = sorted_index_size;

        delete[] sorted_index;
    }
    return std::make_tuple(sorted_array, length);
}



/**
 * @brief Helper function to determine if an array p1 contains another array p2 within it.
 *  p1 contains p2 if there is i,j such that p1[i:j] = p2[i:j] for all i<=x<j
 *  i.e. [1,2,3,4,5] contains [2,3,4] but not [4,5,6] or [2,4,5]
 *
 * @tparam T template type of the array
 * @param p1 sorted unique array to test if contains the sub array
 * @param p2 sorted unique sub array to test if it is contained within the parent array
 * @param l1 length of the parent array
 * @param l2 length of the child array
 * @return true p1 contains p2
 * @return false p1 does not contain p2
 */
template<typename T>
bool array_contains(T* p1, T* p2, size_t l1, size_t l2)
{
    // p1 can not contain p2 if it's size is less than p1
    if (l1 < l2)
    {
        return false;
    }

    size_t j = 0;
    for (size_t i = 0; i < l1; i++)
    {
        // search for first value that matches 
        if (p1[i] != p2[j])
        {
            j++;
            continue;
        }

        // found an element that doest not match 
        if (p1[i] != p2[j])
        {
            return false;
        }

        // reached the end of the sub array with all elements batching 
        if (i == l2)
        {
            break;
        }
    }

    return true;
}
/**
 * @brief search for element in array and return it's index if found
 *
 * @tparam T type of array to search
 * @param p1 pointer to first element in the array
 * @param l1 length of the array
 * @param element element to search for
 * @return optional<size_t> nullopt if note found, else the index it is at
 */
template<typename T>
optional<size_t> array_find(T* p1, size_t l1, T element)
{
    for (size_t i = 0; i < l1; i++)
    {
        if (p1[i] == element)
        {
            return i;
        }
    }

    return nullopt;
}

template<typename T, typename Func>
optional<T> vector_get(const vector<T>& vec, Func func)
{
    // func must take type T and return bool as to wether it is the item to remove
    static_assert(std::is_invocable_r<bool, Func, T>::value, "Func must take parameter of type T and return type bool.");

    auto it = std::find_if(
        vec.begin(),
        vec.end(),
        func);

    if (it == vec.end())
    {
        // item is not found
        return nullopt;
    }
    return *it;
}
