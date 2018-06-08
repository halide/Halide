#ifndef HALIDE_SKIP_STAGES
#define HALIDE_SKIP_STAGES

#include "IR.h"

/** \file
 * Defines a pass that dynamically avoids realizing unnecessary stages.
 */

namespace Halide {
namespace Internal {

/** Avoid computing certain stages if we can infer a runtime condition
 * to check that tells us they won't be used. Does this by analyzing
 * all reads of each buffer allocated, and inferring some condition
 * that tells us if the reads occur. If the condition is non-trivial,
 * inject ifs that guard the production. */
Stmt skip_stages(Stmt s, const std::vector<std::string> &order);

}  // namespace Internal
}  // namespace Halide

#endif
