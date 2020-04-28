#ifndef SUPER_SIMPLIFY_H
#define SUPER_SIMPLIFY_H

#include "Halide.h"

// Use CEGIS to construct an equivalent expression to the input of the given size.
Halide::Expr super_simplify(Halide::Expr e, int size);

#endif
