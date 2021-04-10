#ifndef HANNK_INTERVAL_H
#define HANNK_INTERVAL_H

#include <iostream>
#include <vector>

#include "HalideBuffer.h"

namespace hannk {

// Divide a by b, rounding up or down.
int ceil_div(int a, int b);
int floor_div(int a, int b);

// Align x up or down to a multiple of n.
int align_up(int x, int n);
int align_down(int x, int n);

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
        max = ceil_div(max + 1, scale) - 1;
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

bool is_empty(const Box &a);

}  // namespace hannk

#endif  // HANNK_INTERVAL_H
