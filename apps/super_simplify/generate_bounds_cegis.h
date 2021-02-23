#ifndef GENERATE_BOUNDS_H
#define GENERATE_BOUNDS_H

#include "Halide.h"

// Use CEGIS to construct a bound to the given expression, in terms of mins and maxs of the variables.
// If upper -> upper bound, else lower bound.
Halide::Expr generate_bound(Halide::Expr e, bool upper, int size);

#endif // GENERATE_BOUNDS_H
