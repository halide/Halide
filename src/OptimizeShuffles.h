#ifndef OPTIMIZE_SHUFFLES_H
#define OPTIMIZE_SHUFFLES_H

/** \file
 * Defines a lowering pass that partitions loop bodies into three
 * to handle boundary conditions: A prologue, a simplified
 * steady-stage, and an epilogue.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/* Replace indirect loads with dynamic_shuffle intrinsics where
possible. */
Stmt optimize_shuffles(Stmt s, int lut_alignment);

}  // namespace Internal
}  // namespace Halide

#endif
