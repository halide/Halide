#ifndef HALIDE_ALLOCATION_BOUNDS_INFERENCE_H
#define HALIDE_ALLOCATION_BOUNDS_INFERENCE_H

/** \file
 * Defines the lowering pass that determines how large internal allocations should be.
 */
#include <map>
#include <string>
#include <utility>

#include "Expr.h"
#include "Interval.h"

namespace Halide {
namespace Internal {

class Function;
struct Interval;

/** Take a partially statement with Realize nodes in terms of
 * variables, and define values for those variables. */
Stmt allocation_bounds_inference(Stmt s,
                                 const std::map<std::string, Function> &env,
                                 const std::map<std::pair<std::string, int>, Interval> &func_bounds);
}  // namespace Internal
}  // namespace Halide

#endif
