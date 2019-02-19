#ifndef HALIDE_BOUND_SMALL_ALLOCATIONS
#define HALIDE_BOUND_SMALL_ALLOCATIONS

#include "IR.h"

/** \file
 * Defines the lowering pass that attempts to rewrite small
 * allocations to have constant size.
 */

namespace Halide {
namespace Internal {

/** \file
 *
 * Use bounds analysis to attempt to bound the sizes of small
 * allocations. Inside GPU kernels this is necessary in order to
 * compile. On the CPU this is also useful, because it prevents malloc
 * calls for (provably) tiny allocations. */
Stmt bound_small_allocations(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
