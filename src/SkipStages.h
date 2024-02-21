#ifndef HALIDE_SKIP_STAGES
#define HALIDE_SKIP_STAGES

#include <map>
#include <string>
#include <vector>

#include "Expr.h"

/** \file
 * Defines a pass that dynamically avoids realizing unnecessary stages.
 */

namespace Halide {
namespace Internal {

class Function;

/** Avoid computing certain stages if we can infer a runtime condition
 * to check that tells us they won't be used. Does this by analyzing
 * all reads of each buffer allocated, and inferring some condition
 * that tells us if the reads occur. If the condition is non-trivial,
 * inject ifs that guard the production. */
Stmt skip_stages(Stmt s, const std::vector<Function> &outputs,
                 const std::vector<std::string> &order,
                 const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
