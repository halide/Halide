#ifndef HANNK_SMALL_VECTOR_H
#define HANNK_SMALL_VECTOR_H

#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <utility>

// This class mimics std::vector, but never dynamically allocates memory.
// It can only grow to Capacity elements.
template<typename T, size_t Capacity>
class SmallVector {
    alignas(alignof(T)) char buf_[Capacity * sizeof(T)];
    size_t size_ = 0;

public:
    SmallVector() = default;
    SmallVector(size_t size) {
        resize(size);
    }
    SmallVector(const SmallVector &copy) {
        for (const T &i : copy) {
            push_back(i);
        }
    }
    SmallVector(SmallVector &&move) {
        for (T &i : move) {
            push_back(std::move(i));
        }
    }
    SmallVector(std::initializer_list<T> values) {
        for (const T &i : values) {
            push_back(i);
        }
    }
    ~SmallVector() {
        resize(0);
    }

    SmallVector &operator=(const SmallVector &assign) {
        resize(0);
        for (const T &i : assign) {
            push_back(i);
        }
        return *this;
    }

    SmallVector &operator=(SmallVector &&move) {
        resize(0);
        for (T &i : move) {
            push_back(std::move(i));
        }
        return *this;
    }

    template<typename Iterator>
    void assign(Iterator begin, Iterator end) {
        clear();
        for (Iterator i = begin; i != end; ++i) {
            push_back(*i);
        }
    }

    void resize(size_t size) {
        assert(size <= Capacity);
        // Default construct the new elements.
        for (size_t i = size_; i < size; ++i) {
            new (&data()[i]) T();
        }
        // Destroy the removed elements.
        for (size_t i = size; i < size_; ++i) {
            data()[i].~T();
        }
        size_ = size;
    }

    void clear() {
        resize(0);
    }

    void push_back(T x) {
        assert(size_ < Capacity);
        new (&data()[size_++]) T(std::move(x));
    }

    template<typename... Args>
    void emplace_back(Args &&...args) {
        assert(size_ < Capacity);
        new (&data()[size_++]) T(std::forward<Args>(args)...);
    }

    void pop_back() {
        assert(size_ > 0);
        resize(size_ - 1);
    }

    using iterator = T *;
    using const_iterator = const T *;

    size_t size() const {
        return size_;
    }

    bool empty() const {
        return size_ == 0;
    }

    T *data() {
        return (T *)&buf_[0];
    }

    const T *data() const {
        return (const T *)&buf_[0];
    }

    iterator begin() {
        return data();
    }

    iterator end() {
        return begin() + size_;
    }

    const_iterator begin() const {
        return data();
    }

    const_iterator end() const {
        return begin() + size_;
    }

    T &at(size_t i) {
        assert(i < size_);
        return data()[i];
    }

    const T &at(size_t i) const {
        assert(i < size_);
        return data()[i];
    }

    T &operator[](size_t i) {
        return data()[i];
    }

    const T &operator[](size_t i) const {
        return data()[i];
    }

    const T &front() const {
        return at(0);
    }

    const T &back() const {
        assert(size_ > 0);
        return at(size_ - 1);
    }
};

template<typename T, size_t Capacity>
inline std::ostream &operator<<(std::ostream &s, const SmallVector<T, Capacity> &v) {
    s << "{";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) {
            s << ", ";
        }
        s << v[i];
    }
    return s << "}";
}

#endif  // HANNK_SMALL_VECTOR_H
