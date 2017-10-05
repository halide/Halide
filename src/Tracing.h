#ifndef HALIDE_TRACING_H
#define HALIDE_TRACING_H

/** \file
 * Defines the lowering pass that injects print statements when tracing is turned on
 */

#include <map>

#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Take a statement representing a halide pipeline, inject calls to
 * tracing functions at interesting points, such as
 * allocations. Should be done before storage flattening, but after
 * all bounds inference. */
Stmt inject_tracing(Stmt, const std::string &pipeline_name,
                    const std::map<std::string, Function> &env,
                    const std::vector<Function> &outputs,
                    const Target &Target);

}
}

#endif
