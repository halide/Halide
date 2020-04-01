#ifndef HALIDE_IR_HEXAGON_OPTIMIZE_H
#define HALIDE_IR_HEXAGON_OPTIMIZE_H

#include "Expr.h"
/** \file
 * Tools for optimizing IR for Hexagon.
 */

namespace Halide {
struct Target;

namespace Internal {

/** Replace indirect and other loads with simple loads + vlut
 * calls. */
Stmt optimize_hexagon_shuffles(const Stmt &s, int lut_alignment);

/** Generate vtmpy instruction if possible */
Stmt vtmpy_generator(Stmt s);

/* Generate vscatter-vgather instructions on Hexagon using VTCM memory.
 * The pass should be run before generating shuffles.
 * Some expressions which generate vscatter-vgathers are:
 *     1. out(x) = lut(foo(x)) -> vgather
 *     2. out(idx(x)) = foo(x) -> vscatter */
Stmt scatter_gather_generator(Stmt s);

/** Hexagon deinterleaves when performing widening operations, and
 * interleaves when performing narrowing operations. This pass
 * rewrites widenings/narrowings to be explicit in the IR, and
 * attempts to simplify away most of the
 * interleaving/deinterleaving. */
Stmt optimize_hexagon_instructions(Stmt s, Target t);

/** Generate deinterleave or interleave operations, operating on
 * groups of vectors at a time. */
//@{
Expr native_deinterleave(const Expr &x);
Expr native_interleave(const Expr &x);
bool is_native_deinterleave(const Expr &x);
bool is_native_interleave(const Expr &x);
//@}

}  // namespace Internal
}  // namespace Halide

#endif
