#ifndef HALIDE_SPECIALIZE_CLAMPED_RAMPS_H
#define HALIDE_SPECIALIZE_CLAMPED_RAMPS_H

/** \file
 * Defines a lowering pass that splits loop bodies into three
 * to handle boundary conditions: A prologue, a simplified
 * steady-stage, and an epilogue.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Split loop bodies into a prologue, a steady state, and an
 * epilogue. Finds the steady state by hunting for use of the 'likely'
 * intrinsic. */
Stmt partition_loops(Stmt s);

}
}

#endif
