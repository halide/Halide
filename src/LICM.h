#ifndef HALIDE_LICM_H
#define HALIDE_LICM_H

/** \file
 * Methods for lifting loop invariants out of inner loops.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Hoist loop-invariants out of inner loops. This is especially
 * important in cases where LLVM would not do it for us
 * automatically. For example, it hoists loop invariants out of cuda
 * kernels. */
Stmt hoist_loop_invariant_values(Stmt);

/** Just hoist loop-invariant if statements as far up as
 * possible. Does not lift other values. It's useful to run this
 * earlier in lowering to simplify the IR. */
Stmt hoist_loop_invariant_if_statements(Stmt);

}  // namespace Internal
}  // namespace Halide

#endif
