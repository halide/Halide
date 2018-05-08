#ifndef HALIDE_INTERNAL_SCHEDULE_FUNCTIONS_H
#define HALIDE_INTERNAL_SCHEDULE_FUNCTIONS_H

/** \file
 *
 * Defines the function that does initial lowering of Halide Functions
 * into a loop nest using its schedule. The first stage of lowering.
 */

#include <map>

#include "IR.h"

namespace Halide {

struct Target;

namespace Internal {

class Function;

/** Build loop nests and inject Function realizations at the
 * appropriate places using the schedule. Returns a flag indicating
 * whether memoization passes need to be run. */
Stmt schedule_functions(const std::vector<Function> &outputs,
                        const std::vector<std::vector<std::string>> &fused_groups,
                        const std::map<std::string, Function> &env,
                        const Target &target,
                        bool &any_memoized);

}  // namespace Internal
}  // namespace Halide

#endif
