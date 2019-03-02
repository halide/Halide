#ifndef HALIDE_ALIGN_LOADS_H
#define HALIDE_ALIGN_LOADS_H

/** \file
 * Defines a lowering pass that rewrites unaligned loads into
 * sequences of aligned loads.
 */
#include "IR.h"
#include "ModulusRemainder.h"
#include "Scope.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Attempt to rewrite unaligned loads from buffers which are known to
 * be aligned to instead load aligned vectors that cover the original
 * load, and then slice the original load out of the aligned
 * vectors. */
Stmt align_loads(Stmt s, int alignment);

}  // namespace Internal
}  // namespace Halide

#endif
