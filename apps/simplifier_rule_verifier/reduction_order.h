#ifndef REDUCTION_ORDER_H
#define REDUCTION_ORDER_H

#include "Halide.h"

bool check_divisors(const Halide::Expr &LHS, const Halide::Expr &RHS);
bool valid_reduction_order(const Halide::Expr &LHS, const Halide::Expr &RHS);

#endif
