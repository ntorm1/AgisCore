#pragma once
#include <memory>

#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <mutex>

#define LOCK_GUARD _mutex.lock();
#define UNLOCK_GUARD _mutex.unlock();

template<typename T>
class AGIS_API StridedPointer {
public:
    // Constructor
    StridedPointer(T* ptr, std::size_t size, std::size_t stride)
        : data(ptr), elementCount(size), strideSize(stride) {}
    StridedPointer() = default;

    ~StridedPointer() = default;

    // Subscript operator overload
    T& operator[](std::size_t index) {
        return data[index * strideSize];
    } 

    // Const version of the subscript operator overload
    const T& operator[](std::size_t index) const {
        return data[index * strideSize];
    }

    T const* get() const { return this->data; }

    // Custom iterator
    class iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        // Constructor
        iterator(T* ptr, std::size_t stride) : ptr(ptr), stride(stride) {}

        // Dereference operator
        reference operator*() const {
            return *ptr;
        }

        // Pointer arithmetic operators
        iterator& operator++() {
            ptr += stride;
            return *this;
        }

        iterator operator++(int) {
            iterator temp = *this;
            ++(*this);
            return temp;
        }

        iterator& operator--() {
            ptr -= stride;
            return *this;
        }

        iterator operator--(int) {
            iterator temp = *this;
            --(*this);
            return temp;
        }

        iterator& operator+=(difference_type n) {
            ptr += n * stride;
            return *this;
        }

        iterator& operator-=(difference_type n) {
            ptr -= n * stride;
            return *this;
        }

        iterator operator+(difference_type n) const {
            iterator temp = *this;
            return temp += n;
        }

        iterator operator-(difference_type n) const {
            iterator temp = *this;
            return temp -= n;
        }

        difference_type operator-(const iterator& other) const {
            return (ptr - other.ptr) / stride;
        }

        // Comparison operators
        bool operator==(const iterator& other) const {
            return ptr == other.ptr;
        }

        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }

        bool operator<(const iterator& other) const {
            return ptr < other.ptr;
        }

        bool operator>(const iterator& other) const {
            return other < *this;
        }

        bool operator<=(const iterator& other) const {
            return !(other < *this);
        }

        bool operator>=(const iterator& other) const {
            return !(*this < other);
        }

    private:
        T* ptr;              // Pointer to the data
        std::size_t stride;  // Size of the stride
    };

    size_t size() const { return this->elementCount; }

    // Begin iterator
    iterator begin() const {
        return iterator(data, strideSize);
    }

    // End iterator
    iterator end() const {
        return iterator(data + (elementCount * strideSize), strideSize);
    }

private:
    T* data;              // Pointer to the data
    std::size_t elementCount;  // Number of elements
    std::size_t strideSize;    // Size of the stride
};


template<typename T>
class ThreadSafeVector {
public:
    void push_back(T element) {
        std::lock_guard<std::mutex> lock(mutex_);
        vector_.push_back(std::move(element));
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return vector_.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        vector_.clear();
    }
    
    std::optional<T> pop_back() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!vector_.empty()) {
            T element = std::move(vector_.back());
            vector_.pop_back();
            return std::move(element);
        }
        return std::nullopt;
    }
    // Iterator types
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;

    // Begin and end functions
    iterator begin() {
        std::lock_guard<std::mutex> lock(mutex_);
        return vector_.begin();
    }

    iterator end() {
        std::lock_guard<std::mutex> lock(mutex_);
        return vector_.end();
    }

    const_iterator begin() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return vector_.begin();
    }

    const_iterator end() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return vector_.end();
    }

private:
    std::vector<T> vector_;
    mutable std::mutex mutex_;
};

template<typename T>
class AGIS_API AgisMatrix {
public:
    // Constructor
    AgisMatrix(T* ptr, std::size_t rows_, std::size_t columns_)
        : _data(ptr), _rows(rows_), _columns(columns_) {}
    AgisMatrix() = default;

    std::size_t rows() const { return this->_rows; }
    std::size_t columns() const { return this->_columns; }

    StridedPointer<T> const column(size_t column_index) const {
        return StridedPointer<T>(
            this->_data + (column_index*this->_rows), 
            this->_rows, 
            1
        );
    }

private:
    T* _data;              // Pointer to the data
    std::size_t _rows;     // Number of rows
    std::size_t _columns;  // Number of columns
};