#ifndef HALIDE_BOUNDS_INFERENCE_H
#define HALIDE_BOUNDS_INFERENCE_H

/** \file
 * Defines the bounds_inference lowering pass.
 */

#include <map>

#include "IR.h"
#include "Bounds.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Take a partially lowered statement that includes symbolic
 * representations of the bounds over which things should be realized,
 * and inject expressions defining those bounds.
 */
Stmt bounds_inference(Stmt,
                      const std::vector<Function> &outputs,
                      const std::vector<std::string> &realization_order,
                      const std::vector<std::vector<std::string>> &fused_groups,
                      const std::map<std::string, Function> &environment,
                      const std::map<std::pair<std::string, int>, Interval> &func_bounds,
                      const Target &target);

}
}

#endif
