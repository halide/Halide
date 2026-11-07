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
struct DeviceCodeLines;

/** Pull loops marked with GPU device APIs to a separate
 * module, and call them through the appropriate host runtime module. If
 * device_code_lines is set, it gets the device code of each GPU API, with
 * where its lines came from in conceptual_stmt, the Stmt s was lowered from
 * (or s itself). If line_info_in_code, the device code says so itself too,
 * with the lines of conceptual_source, a printing of conceptual_stmt, as far
 * as the device code generator can. Some need that printing either way. */
Stmt inject_gpu_offload(const Stmt &s, const Target &host_target, bool any_strict_float,
                        const std::map<std::string, Function> &env,
                        const Stmt &conceptual_stmt = Stmt(),
                        const StmtSourceMap *conceptual_source = nullptr,
                        bool line_info_in_code = false,
                        std::vector<DeviceCodeLines> *device_code_lines = nullptr);

}  // namespace Internal
}  // namespace Halide

#endif
