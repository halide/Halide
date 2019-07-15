#ifndef HALIDE_EMULATE_FLOAT16_MATH_H
#define HALIDE_EMULATE_FLOAT16_MATH_H

/** \file
 * The lowering pass that removes/emulates bfloat16/float16 math on targets that don't natively support it.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Lower all (b)float16s and (b)float16 math to the floating point
 * equivalent. Each arithmetic op is done as float32, with the result
 * cast back to float16 or bfloat16. */
Stmt emulate_float16_math(const Stmt &, const Target &);

/** Cast to/from float and bfloat using bitwise math. */
//@{
Expr float32_to_bfloat16(Expr e);
Expr float32_to_float16(Expr e);
Expr float16_to_float32(Expr e);
Expr bfloat16_to_float32(Expr e);
//@}

}
}

#endif
