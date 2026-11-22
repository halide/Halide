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
struct IRPosition;

/** Pull loops marked with GPU device APIs to a separate
 * module, and call them through the appropriate host runtime module. If
 * line_source is set, a printing of s or of IR that s was lowered from,
 * device_code_lines gets, for the device code in the Buffer of each name:
 * where in line_source each of its lines came from. Device code generators
 * that don't keep track of that have no entry. */
Stmt inject_gpu_offload(const Stmt &s, const Target &host_target, bool any_strict_float,
                        const std::map<std::string, Function> &env,
                        const StmtSourceMap *line_source = nullptr,
                        std::map<std::string, std::vector<IRPosition>> *device_code_lines = nullptr);

}  // namespace Internal
}  // namespace Halide

#endif
