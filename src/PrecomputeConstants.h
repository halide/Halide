#ifndef HALIDE_INTERNAL_PRECOMPUTE_CONSTANTS_H
#define HALIDE_INTERNAL_PRECOMPUTE_CONSTANTS_H

/** \file
 * Defines the lowering pass that evaluates Funcs scheduled with
 * Func::precompute() at compile time and embeds the results as constant
 * Buffers.
 */

#include <map>
#include <string>
#include <vector>

#include "Expr.h"

namespace Halide {

struct Argument;
class Module;
struct Target;

namespace Internal {

class Function;

/** For each Func scheduled with precompute(), check that it is constant,
 * evaluate it by JIT on the host over the (constant) region that allocation
 * bounds inference computed for it, and replace its realization in the Stmt
 * with reads of the resulting embedded Buffer. Identical results (type, shape
 * and contents) share one Buffer, including Buffers already in
 * result_module. Must run after allocation bounds inference and before image
 * checks are added. */
Stmt precompute_constants(const Stmt &s,
                          const std::vector<std::string> &order,
                          const std::map<std::string, Function> &env,
                          const std::vector<Argument> &args,
                          const Target &t,
                          const Module &result_module);

}  // namespace Internal
}  // namespace Halide

#endif
