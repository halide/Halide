#ifndef HALIDE_LOWER_BFLOAT_MATH_H
#define HALIDE_LOWER_BFLOAT_MATH_H

/** \file
 * The lowering pass that removes/emulates bfloat16/float16 math on targets that don't natively support it.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Lower all (b)float16s and (b)float16 math to the floating point equivalent */
Stmt lower_float16_math(const Stmt &, const Target &);

}
}

#endif
