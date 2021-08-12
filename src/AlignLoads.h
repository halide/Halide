#ifndef HALIDE_ALIGN_LOADS_H
#define HALIDE_ALIGN_LOADS_H

/** \file
 * Defines a lowering pass that rewrites unaligned loads into
 * sequences of aligned loads.
 */
#include "Expr.h"

namespace Halide {
namespace Internal {

/** Attempt to rewrite unaligned loads from buffers which are known to
 * be aligned to instead load aligned vectors that cover the original
 * load, and then slice the original load out of the aligned
 * vectors.
 *
 * Types that are less than min_bytes_to_align in size do not have
 * alignment applied. This is intended to make a distinction between
 * data that will be accessed as a scalar and that which will be
 * accessed as a vector.
 */
Stmt align_loads(const Stmt &s, int alignment, int min_bytes_to_align);

}  // namespace Internal
}  // namespace Halide

#endif
