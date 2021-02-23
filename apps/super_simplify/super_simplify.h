#ifndef SUPER_SIMPLIFY_H
#define SUPER_SIMPLIFY_H

#include "Halide.h"

std::pair<Halide::Expr, Halide::Expr> interpreter_expr(std::vector<Halide::Expr> terms, std::vector<Halide::Expr> use_counts,
    std::vector<Halide::Expr> opcodes, Halide::Type desired_type, Halide::Type int_type, int max_leaves);

// Use CEGIS to construct an equivalent expression to the input of the given size.
Halide::Expr super_simplify(Halide::Expr e, int size);

#endif
