#ifndef HALIDE_IR_HEXAGON_OPTIMIZE_H
#define HALIDE_IR_HEXAGON_OPTIMIZE_H

/** \file
 * Tools for optimizing IR for Hexagon.
 */

#include "IR.h"
#include "ModulusRemainder.h"
#include "Scope.h"
namespace Halide {
namespace Internal {

/** Replace indirect and other loads with simple loads + vlut
 * calls. */
Stmt optimize_hexagon_shuffles(Stmt s, int lut_alignment);

/** Generate vtmpy instruction if possible */
Stmt vtmpy_generator(Stmt s);

/** Hexagon deinterleaves when performing widening operations, and
 * interleaves when performing narrowing operations. This pass
 * rewrites widenings/narrowings to be explicit in the IR, and
 * attempts to simplify away most of the
 * interleaving/deinterleaving. */
Stmt optimize_hexagon_instructions(Stmt s, Target t, Scope<ModulusRemainder>& alignment_info);

/** Generate deinterleave or interleave operations, operating on
 * groups of vectors at a time. */
//@{
Expr native_deinterleave(Expr x);
Expr native_interleave(Expr x);
bool is_native_deinterleave(Expr x);
bool is_native_interleave(Expr x);
//@}

}  // namespace Internal
}  // namespace Halide

#endif
