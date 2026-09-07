#ifndef HALIDE_INTERNAL_FAST_MATH_H
#define HALIDE_INTERNAL_FAST_MATH_H

#include "Expr.h"
#include "IR.h"

namespace Halide {
namespace Internal {

bool fast_math_func_has_intrinsic_based_implementation(Call::IntrinsicOp op, DeviceAPI device, const Target &t);

Stmt lower_fast_math_functions(const Stmt &s, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
