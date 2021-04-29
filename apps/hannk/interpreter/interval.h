#ifndef HANNK_INTERVAL_H
#define HANNK_INTERVAL_H

#include <initializer_list>
#include <iostream>
#include <utility>

#include "HalideBuffer.h"

namespace hannk {

// The maximum rank of any shape or array of dimension information.
const int max_rank = 6;

// This class mimics std::vector, but never dynamically allocates memory.
// It can only grow to MaxSize elements.
template <typename T, size_t MaxSize>
class SmallVector {
    alignas(alignof(T)) char buf_[MaxSize * sizeof(T)];
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
        for (T &i : values) {
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
        assert(size <= MaxSize);
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
        assert(size_ < MaxSize);
        new (&data()[size_++]) T(std::move(x));
    }

    template<typename... Args>
    void emplace_back(Args &&...args) {
        assert(size_ < MaxSize);
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
};

// Compute a / b, rounding down.
inline int floor_div(int a, int b) {
    assert(a >= 0 && b >= 0);
    int q = a / b;
    if (q * b != a && ((a < 0) != (b < 0))) {
        q -= 1;
    }
    return q;
}

// Compute a / b, rounding to nearest.
inline int round_div(int a, int b) {
    return floor_div(a + b / 2, b);
}

// Compute a / b, rounding upwards.
inline int ceil_div(int a, int b) {
    return floor_div(a + b - 1, b);
}

// Align x up to the next multiplie of n.
inline int align_up(int x, int n) {
    return ceil_div(x, n) * n;
}

// Align x down to the next multiplie of n.
inline int align_down(int x, int n) {
    return floor_div(x, n) * n;
}

// This type (and Box below) mirrors Halide::Interval, but is not symbolic.
struct Interval {
    int min, max;

    Interval()
        : min(0), max(0) {
    }
    explicit Interval(int point)
        : min(point), max(point) {
    }
    Interval(int min, int max)
        : min(min), max(max) {
    }

    bool empty() const {
        return max < min;
    }
    int extent() const {
        return max - min + 1;
    }
    void set_extent(int extent) {
        max = min + extent - 1;
    }

    Interval &operator*=(int scale) {
        min *= scale;
        max *= scale;
        return *this;
    }

    Interval &operator/=(int scale) {
        assert(min >= 0 && max >= 0);
        min = floor_div(min, scale);
        max = floor_div(max, scale);
        return *this;
    }

    Interval &operator+=(int offset) {
        min += offset;
        max += offset;
        return *this;
    }

    Interval &operator-=(int offset) {
        min -= offset;
        max -= offset;
        return *this;
    }

    Interval &operator+=(const Interval &x) {
        min += x.min;
        max += x.max;
        return *this;
    }

    Interval operator*(int scale) const {
        Interval result(*this);
        result *= scale;
        return result;
    }

    Interval operator/(int scale) const {
        Interval result(*this);
        result /= scale;
        return result;
    }

    Interval operator+(int offset) const {
        Interval result(*this);
        result += offset;
        return result;
    }

    Interval operator-(int offset) const {
        Interval result(*this);
        result -= offset;
        return result;
    }

    Interval operator+(const Interval &x) const {
        Interval result(*this);
        result += x;
        return result;
    }

    bool operator==(const Interval &i) const {
        return min == i.min && max == i.max;
    }
    bool operator!=(const Interval &i) const {
        return min != i.min || max != i.max;
    }
};

inline std::ostream &operator<<(std::ostream &s, const Interval &i) {
    return s << "{" << i.min << ", " << i.max << "}";
}

// TODO: We really need an llvm::SmallVector-like thing here.
// These are rarely more than size 4.
using Box = SmallVector<Interval, max_rank>;

// Check if b fully contains a.
inline bool is_subset_of(const Interval &a, const Interval &b) {
    return a.min >= b.min && a.max <= b.max;
}
bool is_subset_of(const Box &a, const Box &b);

// Check if the union of a and b can be computed exactly.
inline bool is_union_exact(const Interval &a, const Interval &b) {
    return !(a.min > b.max + 1 || b.min > a.max + 1);
}
bool is_union_exact(const Box &a, const Box &b);

inline Interval Union(const Interval &a, const Interval &b) {
    return {std::min(a.min, b.min), std::max(a.max, b.max)};
}
Box Union(const Box &a, const Box &b);
inline Interval intersect(const Interval &a, const Interval &b) {
    return {std::max(a.min, b.min), std::min(a.max, b.max)};
}
Box intersect(Box a, const Box &b);

bool is_empty(const Box &a);

}  // namespace hannk

#endif  // HANNK_INTERVAL_H
