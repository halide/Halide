#ifndef HALIDE_BOUND_CONSTANT_EXTENT_LOOPS_H
#define HALIDE_BOUND_CONSTANT_EXTENT_LOOPS_H

/** \file
 * Defines the lowering pass that enforces a constant extent on all
 * vectorized or unrolled loops.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Replace all loop extents of unrolled or vectorized loops with constants, by
 * substituting and simplifying as needed. If we can't determine a constant
 * extent, but can determine a constant upper bound, inject an if statement into
 * the body. If we can't even determine a constant upper bound, throw a user
 * error. */
Stmt bound_constant_extent_loops(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
