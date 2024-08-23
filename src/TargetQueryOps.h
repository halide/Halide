#ifndef HALIDE_TARGET_QUERY_OPS_H
#define HALIDE_TARGET_QUERY_OPS_H

/** \file
 * Defines a lowering pass to lower all target_is() and target_has() helpers.
 */

#include <map>
#include <string>

namespace Halide {

struct Target;

namespace Internal {

class Function;

void lower_target_query_ops(std::map<std::string, Function> &env, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
