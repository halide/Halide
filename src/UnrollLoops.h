#ifndef HALIDE_UNROLL_LOOPS_H
#define HALIDE_UNROLL_LOOPS_H

#include "Expr.h"
/** \file
 * Defines the lowering pass that unrolls loops marked as such
 */

namespace Halide {
namespace Internal {

/** Take a statement with for loops marked for unrolling, and convert
 * each into several copies of the innermost statement. I.e. unroll
 * the loop. */
Stmt unroll_loops(const Stmt &);

}  // namespace Internal
}  // namespace Halide

#endif
