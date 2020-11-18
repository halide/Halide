#ifndef MODEL_H
#define MODEL_H

#include "interval.h"
#include "app_util.h"

namespace interpret_nn {

bool is_union_exact(const Interval &a, const Interval &b) {
    return !(a.min > b.max + 1 || b.min > a.max + 1);
}

bool is_union_exact(const Box &a, const Box &b) {
    APP_CHECK(a.size() == b.size()) << a.size() << " " << b.size();
    int different_dims = 0;
    int dim = -1;
    for (int i = 0; i < (int)a.size(); i++) {
        if (a[i] != b[i]) {
            different_dims++;
            dim = i;
        }
    }
    if (different_dims == 0) {
        // The shapes are the same, the union is trivial.
        return true;
    } else if (different_dims == 1) {
        // One dim is different. We might be able to produce an exact union.
        return is_union_exact(a[dim], b[dim]);
    } else {
        // More than one dim is different, the union is not a rectangle.
        return false;
    }
}

Interval Union(const Interval &a, const Interval &b) {
    return {std::min(a.min, b.min), std::max(a.max, b.max)};
}

Box Union(const Box &a, const Box &b) {
    APP_CHECK(a.size() == b.size());
    Box result;
    result.resize(a.size());
    for (int i = 0; i < (int)a.size(); i++) {
        result[i] = Union(a[i], b[i]);
    }
    return result;
}

Interval intersect(const Interval &a, const Interval &b) {
    return {std::max(a.min, b.min), std::min(a.max, b.max)};
}

Box intersect(Box a, const Box &b) {
    APP_CHECK(a.size() == b.size());
    for (int i = 0; i < (int)a.size(); i++) {
        a[i] = intersect(a[i], b[i]);
    }
    return a;
}

// Try to remove the values of b from a. This can fail if the result is not a single interval.
bool subtract(Interval &a, const Interval &b) {
    if (b.min <= a.min && b.max >= a.max) {
        // b completely covers a, the result is empty.
        a.set_extent(0);
        return true;
    } else if (b.min > a.min && b.max < a.max) {
        // b leaves behind values above and below.
        return false;
    }

    // b covers either the beginning or the end of a.
    if (b.min <= a.min && b.max + 1 > a.min) {
        a = Interval(b.max + 1, a.max);
        return true;
    } else if (b.max >= a.max && b.min - 1 < a.max) {
        a = Interval(a.min, b.min - 1);
        return true;
    }
    return false;
}

// Subtract b from a if possible.
bool subtract(Box &a, const Box &b) {
    APP_CHECK(a.size() == b.size()) << a.size() << " " << b.size();
    int different_dims = 0;
    int dim = -1;
    for (int i = 0; i < (int)a.size(); i++) {
        if (a[i] != b[i]) {
            different_dims++;
            dim = i;
        }
    }
    if (different_dims == 0) {
        // The shapes are the same. We can just clear a and return.
        a.clear();
        return true;
    } else if (different_dims == 1) {
        // One dimension is different, try to subtract those dimensions.
        return subtract(a[dim], b[dim]);
    } else {
        // More than one dim is different, the result is not a rectangle.
        return false;
    }
}

bool is_empty(const Box &a) {
    if (a.empty()) {
        return true;
    }
    for (const Interval &i : a) {
        if (i.empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace interpret_nn

#endif  // MODEL_H
