#ifndef HALIDE_INTERNAL_LOWER_H
#define HALIDE_INTERNAL_LOWER_H

/** \file
 *
 * Defines the function that generates a statement that computes a
 * Halide function using its schedule.
 */

#include <map>
#include <string>
#include <vector>

#include "Argument.h"
#include "Expr.h"
#include "Module.h"

namespace Halide {

struct Target;

namespace Internal {

class Function;
class IRMutator;

/** Given a vector of scheduled halide functions, create a Module that
 * evaluates it. Automatically pulls in all the functions f depends
 * on. Some stages of lowering may be target-specific. The Module may
 * contain submodules for computation offloaded to another execution
 * engine or API as well as buffers that are used in the passed in
 * Stmt. */
Module lower(const std::vector<Function> &output_funcs,
             const std::string &pipeline_name,
             const Target &t,
             const std::vector<Argument> &args,
             LinkageType linkage_type,
             const std::vector<Stmt> &requirements = std::vector<Stmt>(),
             bool trace_pipeline = false,
             const std::vector<IRMutator *> &custom_passes = std::vector<IRMutator *>());

/** Given a halide function with a schedule, create a statement that
 * evaluates it. Automatically pulls in all the functions f depends
 * on. Some stages of lowering may be target-specific. Mostly used as
 * a convenience function in tests that wish to assert some property
 * of the lowered IR. */
Stmt lower_main_stmt(const std::vector<Function> &output_funcs,
                     const std::string &pipeline_name,
                     const Target &t,
                     const std::vector<Stmt> &requirements = std::vector<Stmt>(),
                     bool trace_pipeline = false,
                     const std::vector<IRMutator *> &custom_passes = std::vector<IRMutator *>());

/** Run the canonical-form prefix of lowering: every Stmt-transforming
 * pass from initial loop-nest creation (schedule_functions) through
 * the final intrinsic-recognition and assert-stripping passes. The
 * returned Stmt is the *canonical form* of the program — the IR
 * against which the implement_with structural matcher operates (see
 * docs/implement_with/DESIGN.md §4.4). Both the use-site pipeline
 * (via lower_impl) and the spec pipeline (via the matcher's
 * spec-lowering entry point) are lowered through this single function
 * so they share an identical canonical form modulo Target-conditional
 * gating.
 *
 * Caller responsibilities (the pre-canonical-form setup): deep_copy +
 * build_environment of the input Functions; lower_target_query_ops;
 * strictify_float; lock_loop_levels; optionally
 * apply_implement_with_directives (use-site only); wrap_func_calls;
 * realization_order; simplify_specializations. Spec lowering omits
 * apply_implement_with_directives (there are no instructions inside
 * instructions).
 *
 * Changes to this function change canonical form and are breaking
 * changes for in-tree instruction declarations. */
Stmt lower_to_canonical_form(const std::vector<Function> &outputs,
                             std::map<std::string, Function> &env,
                             const std::vector<std::string> &order,
                             const std::vector<std::vector<std::string>> &fused_groups,
                             const Target &t,
                             const std::vector<Stmt> &requirements,
                             const std::string &pipeline_name,
                             bool trace_pipeline);

void lower_test();

}  // namespace Internal
}  // namespace Halide

#endif
