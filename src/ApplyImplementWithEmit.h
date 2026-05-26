#ifndef HALIDE_APPLY_IMPLEMENT_WITH_EMIT_H
#define HALIDE_APPLY_IMPLEMENT_WITH_EMIT_H

/** \file
 *
 * Phase 5 of the implement_with pipeline: after the canonical-form
 * prefix has lowered the user pipeline, walk every implement_with
 * directive, re-run the structural matcher to recover spec-to-user
 * name bindings, invoke each Instruction's emit callback, and
 * substitute the returned Stmt for the matched For region.
 *
 * The substitution is positioned in lower_impl after
 * lower_to_canonical_form returns and before custom_passes /
 * set_conceptual_code_stmt. That ordering guarantees:
 *   - the input Stmt is canonical-form IR (the same IR the matcher
 *     was designed to operate on),
 *   - user-injected custom_passes observe the substituted Stmt
 *     (which is typically what users want), and
 *   - Hexagon RPC / GPU offload / parallel-task lowering all see the
 *     final substituted shape.
 *
 * See docs/implement_with/DESIGN.md §4.3, §4.4 and §8.2 Phase 5.
 */

#include <map>
#include <string>

#include "Expr.h"
#include "Function.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** For every implement_with directive recorded on any stage of any Func
 * in `env`, locate the matched For region in `s`, re-run the structural
 * matcher to recover spec-to-user bindings, build a MatchContext, call
 * the Instruction's emit callback, and substitute the returned Stmt
 * for the matched For. Returns the rewritten Stmt.
 *
 * If `env` has no directives, this returns `s` unchanged (and does no
 * matcher work). */
Stmt apply_implement_with_emit(const Stmt &s,
                               std::map<std::string, Function> &env,
                               const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_APPLY_IMPLEMENT_WITH_EMIT_H
