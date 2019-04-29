#ifndef PARTITION_LOOPS_H
#define PARTITION_LOOPS_H

/** \file
 * Defines a lowering pass that partitions loop bodies into three
 * to handle boundary conditions: A prologue, a simplified
 * steady-stage, and an epilogue.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Return true if an expression uses a likely tag that isn't captured
 * by an enclosing Select, Min, or Max. */
bool has_uncaptured_likely_tag(Expr e);

/** Return true if an expression uses a likely tag. */
bool has_likely_tag(Expr e);

/** Strip all likely and likely_if_innermost tags from an expression */
Expr remove_likely_tags(Expr e);

/** Partitions loop bodies into a prologue, a steady state, and an
 * epilogue. Finds the steady state by hunting for use of clamped
 * ramps, or the 'likely' intrinsic. */
Stmt partition_loops(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
