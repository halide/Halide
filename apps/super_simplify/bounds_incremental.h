#ifndef BOUNDS_INCREMENTAL_H
#define BOUNDS_INCREMENTAL_H

#include "Halide.h"

// Use CEGIS to construct an equivalent expression to the input of the given size,
// using an incremental (iterative) method.
Halide::Expr generate_bounds_incremental(Halide::Expr expr, bool upper, int size);

#endif // BOUNDS_INCREMENTAL_H
