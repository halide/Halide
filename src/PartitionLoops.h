#ifndef PARTITION_LOOPS_H
#define PARTITION_LOOPS_H

/** \file
 * Defines a lowering pass that partitions loop bodies into three
 * to handle boundary conditions: A prologue, a simplified
 * steady-stage, and an epilogue.
 */

#include "Expr.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Return true if an expression uses a likely tag that isn't captured by an
 * enclosing Select, Min, or Max. The scope contains all vars that should be
 * considered to have uncaptured likelies. */
bool has_uncaptured_likely_tag(const Expr &e, const Scope<> &scope);

/** Return true if an expression uses a likely tag. The scope contains all vars
 * in scope that should be considered to have likely tags. */
bool has_likely_tag(const Expr &e, const Scope<> &scope);

/** Partitions loop bodies into a prologue, a steady state, and an
 * epilogue. Finds the steady state by hunting for use of clamped
 * ramps, or the 'likely' intrinsic. */
Stmt partition_loops(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
