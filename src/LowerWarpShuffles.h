#ifndef HALIDE_LOWER_WARP_SHUFFLES_H
#define HALIDE_LOWER_WARP_SHUFFLES_H

/** \file
 * Defines the lowering pass that injects CUDA warp shuffle
 * instructions to access storage outside of a GPULane loop.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Rewrite access to things stored outside the loop over GPU lanes to
 * use nvidia's warp shuffle instructions. */
Stmt lower_warp_shuffles(Stmt s, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
