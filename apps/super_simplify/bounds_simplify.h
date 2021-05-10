#ifndef BOUNDS_SIMPLIFY_H
#define BOUNDS_SIMPLIFY_H

#include "Halide.h"

// Use CEGIS to construct an equivalent expression to the input of the given size.
Halide::Expr bounds_simplify(Halide::Expr expr, bool upper, int size);

#endif
