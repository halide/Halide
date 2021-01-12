#ifndef HALIDE_TRACING_H
#define HALIDE_TRACING_H

/** \file
 * Defines the lowering pass that injects print statements when tracing is turned on
 */

#include <map>
#include <string>
#include <vector>

#include "Expr.h"

namespace Halide {

struct Target;

namespace Internal {

class Function;

/** Take a statement representing a halide pipeline, inject calls to
 * tracing functions at interesting points, such as
 * allocations. Should be done before storage flattening, but after
 * all bounds inference. */
Stmt inject_tracing(Stmt, const std::string &pipeline_name,
                    bool trace_pipeline,
                    const std::map<std::string, Function> &env,
                    const std::vector<Function> &outputs,
                    const Target &Target);

}  // namespace Internal
}  // namespace Halide

#endif
