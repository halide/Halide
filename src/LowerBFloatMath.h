#ifndef HALIDE_LOWER_BFLOAT_MATH_H
#define HALIDE_LOWER_BFLOAT_MATH_H

/** \file
 * The lowering pass that removes/emulates bfloat math on targets that don't natively support it.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Lower all bfloats and bfloat math to the floating point equivalent */
Stmt lower_bfloat_math(Stmt);

}
}

#endif
