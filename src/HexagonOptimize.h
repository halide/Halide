#ifndef HALIDE_IR_HEXAGON_OPTIMIZE_H
#define HALIDE_IR_HEXAGON_OPTIMIZE_H

/** \file
 * Tools for optimizing IR for Hexagon.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Replace indirect and other loads with simple loads + vlut
 * calls. */
EXPORT Stmt optimize_hexagon_shuffles(Stmt s, int lut_alignment);

/** Hexagon deinterleaves when performing widening operations, and
 * interleaves when performing narrowing operations. This pass
 * rewrites widenings/narrowings to be explicit in the IR, and
 * attempts to simplify away most of the
 * interleaving/deinterleaving. */
EXPORT Stmt optimize_hexagon_instructions(Stmt s, Target t);

/** Generate deinterleave or interleave operations, operating on
 * groups of vectors at a time. */
//@{
EXPORT Expr native_deinterleave(Expr x);
EXPORT Expr native_interleave(Expr x);
EXPORT bool is_native_deinterleave(Expr x);
EXPORT bool is_native_interleave(Expr x);
//@}

}  // namespace Internal
}  // namespace Halide

#endif
