#ifndef REDUCTION_ORDER_H
#define REDUCTION_ORDER_H

#include "Halide.h"

bool check_divisors(Halide::Expr &LHS, Halide::Expr &RHS);
bool valid_reduction_order(Halide::Expr &LHS, Halide::Expr &RHS);

#endif