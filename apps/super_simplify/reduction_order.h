#ifndef REDUCTION_ORDER_H
#define REDUCTION_ORDER_H

#include "Halide.h"

bool check_divisors(Halide::Expr &LHS, Halide::Expr &RHS);
bool valid_reduction_order(const Halide::Expr &LHS, const Halide::Expr &RHS);

#endif