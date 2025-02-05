#ifndef HALIDE_INTERNAL_FAST_MATH_H
#define HALIDE_INTERNAL_FAST_MATH_H

#include "Expr.h"

namespace Halide {
namespace Internal {

Stmt lower_fast_math_functions(const Stmt &s, const Target &t);

}
}

#endif
