#ifndef HALIDE_APPLY_IMPLEMENT_WITH_H
#define HALIDE_APPLY_IMPLEMENT_WITH_H

/** \file
 *
 * Phase 3 of the implement_with pipeline: at lowering time, walk every
 * implement_with directive in the env, check that the instruction's
 * required Target features are enabled, and transfer the spec Funcs'
 * scheduling directives onto the matched user Funcs so they participate
 * in normal bounds inference. See docs/implement_with/DESIGN.md §4.2–§4.3.
 *
 * Structural matching, match-parameter binding, and emit substitution
 * are still future work (Phases 4–5).
 */

#include <map>
#include <string>
#include <vector>

#include "Function.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** For each implement_with directive recorded on any stage of any Func
 * in `env`, check that `target` enables the instruction's required
 * features and transfer the spec Funcs' scheduling directives onto the
 * corresponding user Funcs. Operates in place on `env`, which is
 * expected to be the deep-copied lowering env (so the user's pristine
 * schedule is not mutated).
 *
 * `outputs` is the deep-copied output Function list (the same one
 * lower_impl already holds). It is used to lower the user pipeline
 * to canonical form so the structural matcher can derive spec-to-user
 * bindings; without it the spec-input transfer is limited to a
 * name-keyed lookup. Passing an empty vector forces the matcher path
 * to short-circuit and the pass falls back to its Phase 3 name-keyed
 * behavior. */
void apply_implement_with_directives(std::map<std::string, Function> &env,
                                     const std::vector<Function> &outputs,
                                     const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_APPLY_IMPLEMENT_WITH_H
