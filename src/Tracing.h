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

/** Make a statement that emits a halide_trace_bounds_required event for the
 * region of f described by the f.s0.<arg>.min/max symbols in scope, with the
 * given parent event id. Returns an undefined Stmt if f's bounds required are
 * not traced. */
Stmt make_trace_bounds_required(const Function &f, const Expr &parent_id,
                                const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
