#ifndef HL_JULIA_LP_H
#define HL_JULIA_LP_H

#include "Halide.h"
#include <string>

std::string expr_to_julia_lp(const Halide::Expr &expr, bool upper);

#endif // HL_JULIA_LP_H
