#ifndef INTERVAL_H
#define INTERVAL_H

#include <iostream>
#include <vector>

#include "HalideBuffer.h"

namespace interpret_nn {

// This type (and Box below) mirrors Halide::Interval, but is not symbolic.
struct Interval {
    int min, max;

    Interval() : min(0), max(0) {}
    Interval(int point) : min(point), max(1) {}
    Interval(int min, int max) : min(min), max(max) {}
    Interval(halide_dimension_t dim) : min(dim.min), max(dim.min + dim.extent - 1) {}

    bool empty() const { return max < min; }
    int extent() const { return max - min + 1; }
    void set_extent(int extent) { max = min + extent - 1; }

    Interval &operator*=(int scale) {
        min *= scale;
        max *= scale;
        return *this;
    }

    Interval &operator/=(int scale) {
        assert(min >= 0 && max >= 0);
        min /= scale;
        max /= scale;
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

    Interval operator+(int scale) const {
        Interval result(*this);
        result += scale;
        return result;
    }

    Interval operator-(int scale) const {
        Interval result(*this);
        result -= scale;
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

inline std::ostream &operator<<(std::ostream &s, const halide_dimension_t &dim) {
    return s << "{" << dim.min << ", " << dim.extent << ", " << dim.stride << "}";
}

template<typename T>
inline std::ostream &operator<<(std::ostream &s, const std::vector<T> &v) {
    s << "{";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) {
            s << ", ";
        }
        s << v[i];
    }
    return s << "}";
}

using Box = std::vector<Interval>;

// Check if b fully contains a.
bool is_subset_of(const Interval &a, const Interval &b);
bool is_subset_of(const Box &a, const Box &b);

// Check if the union of a and b can be computed exactly.
bool is_union_exact(const Interval &a, const Interval &b);
bool is_union_exact(const Box &a, const Box &b);

Interval Union(const Interval &a, const Interval &b);
Box Union(const Box &a, const Box &b);
Interval intersect(const Interval &a, Interval &b);
Box intersect(Box a, const Box &b);

// Try to remove the values of b from a. This can fail if the result is not a single interval.
bool subtract(Interval &a, const Interval &b);
bool subtract(Box &a, const Box &b);

bool is_empty(const Box &a);

}  // namespace interpret_nn

#endif  // INTERVAL_H
