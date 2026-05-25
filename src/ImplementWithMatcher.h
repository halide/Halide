#ifndef HALIDE_IMPLEMENT_WITH_MATCHER_H
#define HALIDE_IMPLEMENT_WITH_MATCHER_H

/** \file
 *
 * Structural matching infrastructure for `implement_with`. This file
 * hosts the Phase 4+ matcher that compares a spec pipeline's
 * canonical-form IR against a region of the user's lowered IR.
 *
 * See docs/implement_with/DESIGN.md §4.4 for the role of canonical
 * form in the matcher.
 */

#include <string>

#include "Expr.h"

namespace Halide {

struct Target;
class Pipeline;

namespace Internal {

/** Lower a spec pipeline (typically produced by Instruction::spec())
 * to canonical form for the implement_with structural matcher.
 *
 * This is the spec-side counterpart of lower_impl's prefix: it runs
 * the same pre-canonical-form setup steps (deep_copy +
 * build_environment, lower_target_query_ops, strictify_float,
 * lock_loop_levels, wrap_func_calls, realization_order,
 * simplify_specializations) and then dispatches to
 * lower_to_canonical_form. The Phase 3 schedule-transfer pass
 * (apply_implement_with_directives) is NOT run on spec pipelines:
 * specs cannot themselves carry implement_with directives.
 *
 * Both this entry point and the use-site lowering (lower_impl)
 * dispatch to the same lower_to_canonical_form, so a spec lowered
 * with this function and a user pipeline lowered through lower_impl
 * produce IR that is comparable structurally modulo the
 * Target-conditional gates noted in DESIGN.md §4.4. */
Stmt lower_spec_to_canonical_form(const Pipeline &spec, const Target &t);

/** Locate the For node in a canonical-form Stmt that corresponds to
 * an implement_with directive's loop level. The directive is keyed by
 * (user_func_name, stage_index, loop_var_name); the For node we look
 * for is named "<user_func_name>.s<stage_index>.<loop_var_name>",
 * which is the format produced by schedule_functions and preserved by
 * uniquify_variable_names.
 *
 * Returns an undefined Stmt if no such For node exists. The match is
 * exact: split-renamed Vars (e.g. xi from a split of x) must be named
 * with their post-split name in the directive, just as they would be
 * in any other scheduling call. */
Stmt find_implement_with_loop(const Stmt &s,
                              const std::string &user_func_name,
                              int stage_index,
                              const std::string &loop_var_name);

}  // namespace Internal
}  // namespace Halide

#endif
