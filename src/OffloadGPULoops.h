#ifndef HALIDE_OFFLOAD_GPU_LOOPS_H
#define HALIDE_OFFLOAD_GPU_LOOPS_H

/** \file
 * Defines a lowering pass to pull loops marked with
 * GPU device APIs to a separate module, and call them through the
 * appropriate host runtime module.
 */

#include <map>
#include <string>

#include "Expr.h"
#include "Function.h"

namespace Halide {

struct Target;

namespace Internal {

struct StmtSourceMap;

/** Pull loops marked with GPU device APIs to a separate
 * module, and call them through the appropriate host runtime module. If
 * debug_source is set, it is a printing of s, which the device code gets debug
 * info for, as far as the device code generator can. */
Stmt inject_gpu_offload(const Stmt &s, const Target &host_target, bool any_strict_float,
                        const std::map<std::string, Function> &env,
                        const StmtSourceMap *debug_source = nullptr);

}  // namespace Internal
}  // namespace Halide

#endif
