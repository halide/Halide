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
Stmt inject_tracing(Stmt, std::string_view pipeline_name,
                    bool trace_pipeline,
                    const StringMap<Function> &env,
                    const std::vector<Function> &outputs,
                    const Target &Target);

}  // namespace Internal
}  // namespace Halide

#endif
