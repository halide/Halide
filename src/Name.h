#ifndef HALIDE_INTERNAL_NAME_H
#define HALIDE_INTERNAL_NAME_H

#include "Expr.h"
#include <string>

namespace Halide {
namespace Internal {

/** Various helpers to for operating on internal strings with meaningful
 * prefixes or suffixes. Ideally we'd use strings less, but this at least puts
 * some of it behind a layer of abstraction to make future work on making names
 * more structured easier. */

class Name {
    std::string s;

public:
    Name() = default;
    Name(const Name &) = default;
    Name(Name &&) = default;
    Name(const std::string &s)
        : s(s) {
    }
    Name(const char *s)
        : s(s) {
    }

    Name &operator=(const Name &) = default;
    Name &operator=(Name &&) = default;

    Name append(const Name &suffix) const;
    Name append(const std::string &suffix) const;
    Name append(const char *suffix) const;
    Name append(int i) const;
    Expr qualify(const Expr &expr) const;
    Name min() const;
    Name max() const;
    Name loop_max() const;
    Name loop_min() const;
    Name loop_extent() const;
    Name outer_min() const;
    Name outer_max() const;
    Name min_realized(int dim) const;
    Name max_realized(int dim) const;
    Name extent_realized(int dim) const;
    Name total_extent(int dim) const;
    Name total_extent_bytes() const;
    Name stride(int dim) const;
    Name extent(int dim) const;
    Name min(int dim) const;
    Name tuple_component(int tuple_index) const;
    Name buffer() const;
    Name stage(int stage) const;
    bool starts_with(const Name &prefix) const;
    bool ends_with(const Name &var) const;
    Name bounds_query() const;
    Name bounds_query(const Name &func) const;
    Name outer_bounds_query() const;
    Name output(int i) const;
    Name unbounded() const;
    Name suffix() const;
    bool is_compound() const;
    Name guarded() const;

    bool empty() const {
        return s.empty();
    }

    const std::string &str() const {
        return s;
    }

    bool operator==(const Name &other) const {
        return s == other.s;
    }
};

inline bool operator==(const std::string &a, const Name &b) {
    return a == b.str();
}

inline bool operator<(const Name &a, const Name &b) {
    return a.str() < b.str();
}

}  // namespace Internal
}  // namespace Halide

#endif
