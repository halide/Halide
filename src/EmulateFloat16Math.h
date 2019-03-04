#ifndef HALIDE_EMULATE_FLOAT16_MATH_H
#define HALIDE_EMULATE_FLOAT16_MATH_H

/** \file
 * The lowering pass that removes/emulates bfloat16/float16 math on targets that don't natively support it.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Lower all (b)float16s and (b)float16 math to the floating point
 * equivalent. Each mathematic op is done as float32, with the result
 * truncated back to float16 or bfloat16. Casts are implemented using
 * bitwise logic. */
Stmt emulate_float16_math(const Stmt &, const Target &);

}
}

#endif
