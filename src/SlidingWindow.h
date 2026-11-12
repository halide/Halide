#ifndef HALIDE_SLIDING_WINDOW_H
#define HALIDE_SLIDING_WINDOW_H

/** \file
 *
 * Defines the sliding_window lowering optimization pass, which avoids
 * computing provably-already-computed values.
 */

#include <map>
#include <string>

#include "Expr.h"

namespace Halide {
namespace Internal {

class Function;

/** Perform sliding window optimization on a Stmt. This is the
 * optimization that turns a stencil pipeline like this:
 *
 * \code
 *   for y:
 *     produce f over [y-1, y+1]   // three rows, one per iteration
 *     consume f to make g(y)
 * \endcode
 *
 * into this:
 *
 * \code
 *   for y:
 *     produce f over [y+1, y+1]   // one row, reusing the two from last time
 *     consume f to make g(y)
 * \endcode
 *
 * It applies when a Func's storage outlives its computation (store_at is
 * outside compute_at), and the region of it required moves monotonically as
 * some serial loop between those two levels advances. Then each iteration only
 * needs to compute the sliver of the Func that the previous iteration didn't.
 *
 * The wrinkle is the first iteration, which has no previous iteration to
 * inherit from. There are two ways this pass deals with that:
 *
 * - Explicit warm-up. The region computed becomes a select: on the first
 *   iteration compute the whole thing, and after that just the sliver. This
 *   always applies, but it leaves a select in the bounds, which is bad for
 *   loop partitioning and for anything downstream that wants simple bounds.
 *
 * - Implicit warm-up, by rewinding the loop. We instead back the loop min up
 *   by however many iterations it takes for the slivers to add up to the whole
 *   region, and leave the steady-state bounds alone. The extra iterations at
 *   the front produce nothing but the warm-up, so everything in the loop body
 *   that isn't part of warming a window up must be guarded to only run from
 *   the original loop min onwards. This is the preferred form, and is what
 *   makes this pass tricky, because several Funcs may want to rewind the same
 *   loop by different amounts, and their warm-ups have to interleave
 *   correctly: a Func's warm-up iterations may need values from a Func
 *   upstream of it, which must therefore start warming up even earlier.
 *
 * A Func with MemoryType::Register slides differently. The region it writes
 * doesn't shrink at all. Instead each store becomes a select: on the leading
 * edge of the window it computes the value, and everywhere else it copies the
 * value the previous iteration left in a register. With the loops unrolled,
 * those copies cost nothing.
 *
 * Worked example. For:
 *
 * \code
 *   f(x) = ...;
 *   g(x) = f(x) + f(x - 1);
 *   f.store_root().compute_at(g, x);
 * \endcode
 *
 * the loop nest arrives at this pass looking like this:
 *
 * \code
 *   for (g.s0.x, g.s0.x.loop_min, g.s0.x.loop_max) {
 *     let f.s0.x.min = g.s0.x + -1
 *     let f.s0.x.max = g.s0.x
 *     produce f { for (f.s0.x, f.s0.x.min, f.s0.x.max) { f(f.s0.x) = ... } }
 *     consume f { g(g.s0.x) = f(g.s0.x) + f(g.s0.x - 1) }
 *   }
 * \endcode
 *
 * and leaves looking like this. The region required of f has shrunk to the one
 * value per iteration that the previous iteration didn't compute, the loop
 * starts an iteration earlier so that there always is a previous iteration,
 * and g is guarded so that it doesn't run on that extra iteration:
 *
 * \code
 *   let g.s0.x.$n.loop_min = g.s0.x.loop_min + -1
 *   for (g.s0.x.$n, g.s0.x.$n.loop_min, g.s0.x.loop_max) {
 *     let f.s0.x.min = max(g.s0.x.loop_min + -1, g.s0.x.$n)
 *     let f.s0.x.max = g.s0.x.$n
 *     produce f { for (f.s0.x, f.s0.x.min, f.s0.x.max) { f(f.s0.x) = ... } }
 *     consume f {
 *       if (likely_if_innermost(g.s0.x.loop_min <= g.s0.x.$n)) {
 *         g(g.s0.x.$n) = f(g.s0.x.$n) + f(g.s0.x.$n - 1)
 *       }
 *     }
 *   }
 * \endcode
 *
 * The max in f's new min is what stops the warm-up iteration computing values
 * from before the start of the region g will ever ask for.
 *
 * Second worked example, showing why one guard isn't enough. For:
 *
 * \code
 *   e(x) = ...;
 *   f(x) = e(x) + e(x - 3);
 *   g(x) = f(x) + f(x - 1);
 *   h(x) = g(x) + g(x - 1) + e(x);
 *   e.store_root().compute_at(h, x);
 *   f.compute_at(h, x);            // not sliding
 *   g.store_root().compute_at(h, x);
 * \endcode
 *
 * e needs five warm-up iterations and g needs one, so the loop rewinds by
 * five, and three different things start at three different iterations:
 *
 * \code
 *   for (h.s0.x.$n.$n, min(h.s0.x.loop_min - 5, h.s0.x.loop_min - 1), ...) {
 *     produce e { ... }                             // warming up: no guard
 *     consume e {
 *       if (likely_if_innermost(h.s0.x.loop_min + -1 <= h.s0.x.$n.$n)) {
 *         produce f { ... }                         // g's warm-up needs this,
 *       }                                           // e's does not
 *       consume f {
 *         produce g { ... }                         // warming up: no guard
 *         consume g {
 *           if (likely_if_innermost(h.s0.x.loop_min <= h.s0.x.$n.$n)) {
 *             h(h.s0.x.$n.$n) = ...                 // the real work
 *           }
 *         }
 *       }
 *     }
 *   }
 * \endcode
 *
 * f must run during g's warm-up, or g warms up with garbage. It must not run
 * during e's, both because that's wasted work and because a Func that is
 * itself sliding over some enclosing loop would have its own window corrupted
 * by the extra iterations.
 */
Stmt sliding_window(const Stmt &s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
