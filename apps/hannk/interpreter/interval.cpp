#include "interpreter/interval.h"

namespace hannk {

bool is_subset_of(const Box &a, const Box &b) {
    assert(a.size() == b.size());
    for (int i = 0; i < (int)a.size(); i++) {
        if (!is_subset_of(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

bool is_union_exact(const Box &a, const Box &b) {
    if (is_subset_of(a, b) || is_subset_of(b, a)) {
        return true;
    }
    assert(a.size() == b.size());
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

Box Union(const Box &a, const Box &b) {
    assert(a.size() == b.size());
    Box result;
    result.resize(a.size());
    for (int i = 0; i < (int)a.size(); i++) {
        result[i] = Union(a[i], b[i]);
    }
    return result;
}

Box intersect(Box a, const Box &b) {
    assert(a.size() == b.size());
    for (int i = 0; i < (int)a.size(); i++) {
        a[i] = intersect(a[i], b[i]);
    }
    return a;
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

}  // namespace hannk
