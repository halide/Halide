#ifndef HALIDE_ALLOCATION_BOUNDS_INFERENCE_H
#define HALIDE_ALLOCATION_BOUNDS_INFERENCE_H

/** \file
 * Defines the lowering pass that determines how large internal allocations should be.
 */

#include "Bounds.h"
#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a partially statement with Realize nodes in terms of
 * variables, and define values for those variables. */
Stmt allocation_bounds_inference(Stmt s,
                                 const std::map<std::string, Function> &env,
                                 const std::map<std::pair<std::string, int>, Interval> &func_bounds);
}  // namespace Internal
}  // namespace Halide

#endif
