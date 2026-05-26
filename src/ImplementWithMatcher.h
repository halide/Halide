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

/** Lower an arbitrary user pipeline to canonical form via the same
 * pre-canonical-form prefix as `lower_spec_to_canonical_form`. Differs
 * from the spec variant only in that it does not assert that the
 * pipeline's outputs are spec-pattern Funcs.
 *
 * Intended for situations (e.g. matcher case-study tests, or any
 * future user-IR comparison) where the caller wants a canonical-form
 * Stmt for a real Halide pipeline without going through the full
 * compile path. As with the spec variant, the `implement_with`
 * directive hook is not invoked. */
Stmt lower_pipeline_to_canonical_form(const Pipeline &p, const Target &t);

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

/** Locate the spec primary's outermost compute For at a given stage,
 * looking only inside the producer-for-`spec_out_name` subtree.
 *
 * Used by the `apply_implement_with_directives` wire-in: the
 * directive carries the user's bare loop_var_name (so the user-side
 * For can be found by name via `find_implement_with_loop`), but
 * carries no analogous hint for the spec side. The spec's "matched
 * loop" for a stage is, by convention, its outermost For inside that
 * producer at that stage --- which is what the spec author scheduled
 * the matched region to start at. If the spec author splits a Var
 * (e.g. `out.update(0).split(i, io, ii, 64)`), the post-split outer
 * For (named `<spec_out>.s<stage>.i.io`) is the outermost and is
 * returned.
 *
 * Returns an undefined Stmt if no matching For is found. */
Stmt find_spec_primary_loop(const Stmt &s,
                            const std::string &spec_out_name,
                            int stage_index);

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
 *  - When structural recursion fails on a pair of scalar/vector
 *    integer Exprs, the matcher falls back to a Simplify-equivalence
 *    check: it substitutes the current `var_rename` bindings into the
 *    spec side and asks Simplify whether `spec_substituted - user`
 *    reduces to a constant zero. This lets algebraically equivalent
 *    but lexically distinct integer indices (e.g. `(i + 4) - 2` vs
 *    `i + 2`, given a binding `i -> j`) match without depending on
 *    Simplify's pass-time normalization. The fallback is only
 *    consulted after structural dispatch fails, so structurally
 *    identical IR continues to match without paying for an extra
 *    Simplify call. */
MatchResult match_canonical_form(const Stmt &spec_loop,
                                 const Stmt &user_loop);

}  // namespace Internal
}  // namespace Halide

#endif
