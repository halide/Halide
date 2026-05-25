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

#include <map>
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

/** Outcome of a single structural-match attempt between a spec's
 * canonical-form loop region and the user's. Owns no IR; the rename
 * maps are bindings discovered during the parallel walk.
 *
 * On success:
 *  - `var_rename` maps spec-side Variable / For-loop / Let{,Stmt}
 *    bound names to their user-side counterparts. Identity bindings
 *    are recorded; the map is non-empty whenever any name appears.
 *  - `func_rename` maps spec-side buffer / Func / Realize / Provide
 *    names to their user-side counterparts. Spec input Funcs (the
 *    auto-stubbed handles produced by Phase 2's spec-pattern Func
 *    mode) appear here bound to whichever user Func filled the
 *    corresponding role.
 *  - `failure_reason` is empty.
 *
 * On failure:
 *  - `failure_reason` is a short human-readable string ending with a
 *    period. Both rename maps may contain partial bindings made
 *    before the mismatch; callers should treat them as undefined. */
struct MatchResult {
    bool success = false;
    std::string failure_reason;
    std::map<std::string, std::string> var_rename;
    std::map<std::string, std::string> func_rename;
};

/** Structurally match a spec pipeline's canonical-form loop against a
 * region of the user's canonical-form loop, producing alpha-renaming
 * bindings on success. Both arguments are For nodes (typically those
 * returned by find_implement_with_loop on the respective canonical
 * Stmts). Match rules:
 *  - Variables, For/LetStmt/Let bound names, and buffer/Func names
 *    in Load/Store/Call/Provide/Realize nodes are alpha-renamed.
 *    The matcher binds a spec name to a user name on first sight and
 *    enforces consistency thereafter.
 *  - The commutative ops Add, Mul, Min, Max try both child orderings.
 *  - Types, opcode kinds, and Call::call_type must match exactly.
 *
 * The matcher does not yet call Simplify on integer subexpressions, so
 * lexically distinct but algebraically equivalent indices (e.g.
 * `i + 1` vs `1 + i` outside a commutative-op context) will not
 * match. That refinement is a follow-up commit. */
MatchResult match_canonical_form(const Stmt &spec_loop,
                                 const Stmt &user_loop);

}  // namespace Internal
}  // namespace Halide

#endif
