#ifndef HALIDE_LICM_H
#define HALIDE_LICM_H

/** \file
 * Methods for lifting loop invariants out of inner loops.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Hoist loop-invariants out of inner loops. This is especially
 * important in cases where LLVM would not do it for us
 * automatically. For example, it hoists loop invariants out of cuda
 * kernels. */
Stmt loop_invariant_code_motion(Stmt);

}  // namespace Internal
}  // namespace Halide

#endif
