#ifndef GENERATE_BOUNDS_H
#define GENERATE_BOUNDS_H

#include "Halide.h"

#include <map>
#include <string>

// Use CEGIS to construct a bound to the given expression, in terms of mins and maxs of the variables.
// If upper -> upper bound, else lower bound.
Halide::Expr generate_bound(Halide::Expr e, bool upper, int size, int max_leaves);

Halide::Internal::Scope<Halide::Internal::Interval> make_symbolic_scope(const Halide::Expr &expr);

int count_leaves(const Halide::Expr &expr);

// helpful for debugging
void print_counterexample(std::map<std::string, Halide::Expr> counterexample);

#endif // GENERATE_BOUNDS_H
