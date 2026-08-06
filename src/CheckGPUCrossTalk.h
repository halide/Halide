#ifndef HALIDE_CHECK_GPU_CROSS_TALK_H
#define HALIDE_CHECK_GPU_CROSS_TALK_H

/** \file
 *
 * Defines a lowering pass that checks that allocations in memory private to a
 * GPU thread are not shared between GPU threads.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Check that no allocation in memory private to a GPU thread, scheduled
 * outside the loops over GPU threads, is accessed by more than one thread.
 * Such an allocation gives every thread its own copy rather than one copy
 * shared by the block, so a thread that touches a part of it another thread
 * was responsible for gets its own copy of that part instead. Raises a user
 * error if it cannot prove each thread keeps to its own part.
 *
 * Validation usually wants to be as early in lowering as possible, but this
 * check is pinned between two passes. It must run before storage flattening,
 * while the arguments of an access are still separated by dimension: once they
 * have been flattened into a single index, showing that two threads cannot
 * meet requires undoing the flattening, which needs modular arithmetic the
 * simplifier will not do. It must run after storage folding, which rewrites
 * access indices modulo a fold factor, and so can make two threads that were
 * touching separate parts of an allocation touch the same part. */
void check_gpu_cross_talk(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
