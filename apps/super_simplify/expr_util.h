#ifndef EXPR_UTIL_H
#define EXPR_UTIL_H

#include "Halide.h"

// Utilities for analyzing Halide Exprs

// Find all the free variables in a Halide Expr and return how many
// times each is used.
std::map<std::string, std::pair<Halide::Expr, int>> find_vars(const Halide::Expr &e);

// Does expr a describe a pattern that expr b would match. For example
// more_general_than(x + y, (x*3) + y) returns true. bindings is an
// in-out parameter. If some var in a is already in the bindings, it
// has to match the expression it is bound to exactly in b. If some
// var in a isn't in the binding, then the corresponding expression in
// b is added.
bool more_general_than(const Halide::Expr &a,
                       const Halide::Expr &b,
                       std::map<std::string, Halide::Expr> &bindings,
                       bool must_match_all_of_b = false);

inline bool more_general_than(const Halide::Expr &a, const Halide::Expr &b) {
    std::map<std::string, Halide::Expr> bindings;
    return more_general_than(a, b, bindings);
}

#endif
