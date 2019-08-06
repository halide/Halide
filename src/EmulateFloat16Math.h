#ifndef HALIDE_EMULATE_FLOAT16_MATH_H
#define HALIDE_EMULATE_FLOAT16_MATH_H

/** \file
 * The lowering pass that removes/emulates bfloat16/float16 math on targets that don't natively support it.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Lower all (b)float16s and (b)float16 math to intrinsics
 * operating on uint types (e.g. + on Float(16) becomes add_f16
 * on two uint16s).  */
Stmt use_intrinsics_for_float16_math(const Stmt &, const Target &);

/** Implement the intrinsics above using 32-bit floating point
 * operations and bit manipulation. Done during code generation. */
Expr lower_float16_intrinsics_to_float32_math(const Expr &e);

/** Check if a call is one of the intrinsics introduced above, or some
 * other float16 transcendental (like sqrt_f16) */
bool is_float16_intrinsic(const Call *);

/** Cast to/from float and bfloat using bitwise math. */
//@{
Expr float32_to_bfloat16(Expr e);
Expr float32_to_float16(Expr e);
Expr float16_to_float32(Expr e);
Expr bfloat16_to_float32(Expr e);

Expr lower_float16_cast(const Cast *op);
//@}

}
}

#endif
