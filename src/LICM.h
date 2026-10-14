#ifndef HALIDE_LICM_H
#define HALIDE_LICM_H

/** \file
 * Methods for lifting loop invariants out of inner loops.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Hoist loop invariants out of loops, and bind values computed more than
 * once to a single let.
 *
 * The simplifier peels cheap operations (+, -, *, /, %, min, max against a
 * constant or a variable) off the value of a let and pushes them into every use
 * of the variable, so that after the last simplification the same arithmetic is
 * repeated at each use site, and loop invariants sit inside loops. This pass
 * finds, per scalar variable (a let, a loop variable, or a free variable such as
 * a parameter), the chains of pure nodes its uses apply to it: arithmetic,
 * comparisons, logic, selects, narrowing casts and pure calls, where every other
 * child of a node is a pure expression available where the variable is bound,
 * possibly itself such a chain. Pure expressions over constants alone form
 * chains of their own. A let whose value is a chain of another variable is an
 * alias of that chain, so lets with equal values share one trie.
 *
 * Every chain prefix shared by two or more uses is bound to a new variable,
 * named after the original with a .licm suffix, and so is a chain used once
 * when that moves it out of a loop. When a let-bound variable is only ever used
 * through one chain, that chain is folded into the value of the original let
 * instead. A new let goes at the innermost loop, let or if-branch body
 * containing its uses, unless that lies inside a loop the value is invariant
 * in: then it goes just around the outermost such loop, so the work is neither
 * repeated per iteration nor moved above anything else. Uses inside a gpu
 * kernel get a let inside the kernel, so kernels don't gain arguments.
 *
 * Runs after the last simplification, which would otherwise peel the new lets
 * apart again. */
Stmt hoist_loop_invariant_values(const Stmt &);

/** Just hoist loop-invariant if statements as far up as
 * possible. Does not lift other values. It's useful to run this
 * earlier in lowering to simplify the IR. */
Stmt hoist_loop_invariant_if_statements(const Stmt &);

}  // namespace Internal
}  // namespace Halide

#endif
