#ifndef HALIDE_LICM_H
#define HALIDE_LICM_H

/** \file
 * Methods for lifting loop invariants out of inner loops.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Hoist loop-invariant variable and conditions out of inner loops.
 * This is especially important in cases where LLVM would not do it for us
 * automatically. For example, it hoists loop invariants out of cuda
 * kernels. By default it uses a heuristic cost function to decide
 * which variable to hoist out. If always_lift is set to false
 * all loop invariant variables are lifted out.*/
Stmt loop_invariant_code_motion(Stmt, bool always_lift = false);

}  // namespace Internal
}  // namespace Halide

#endif
