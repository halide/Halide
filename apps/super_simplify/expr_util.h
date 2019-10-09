#ifndef EXPR_UTIL_H
#define EXPR_UTIL_H

#include "Halide.h"

// Utilities for analyzing Halide Exprs

// Find all the free variables in a Halide Expr and return how many
// times each is used.
std::map<std::string, int> find_vars(const Halide::Expr &e);

#endif
