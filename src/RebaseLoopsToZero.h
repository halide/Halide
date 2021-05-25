#ifndef HALIDE_REBASE_LOOPS_TO_ZERO_H
#define HALIDE_REBASE_LOOPS_TO_ZERO_H

/** \file
 * Defines the lowering pass that rewrites loop mins to be 0.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Rewrite the mins of most loops to 0. */
Stmt rebase_loops_to_zero(const Stmt &);

}  // namespace Internal
}  // namespace Halide

#endif
