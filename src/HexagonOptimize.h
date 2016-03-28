#ifndef HALIDE_IR_HEXAGON_OPTIMIZE_H
#define HALIDE_IR_HEXAGON_OPTIMIZE_H

/** \file
 * Tools for optimizing IR for Hexagon.
 */

#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Hexagon deinterleaves when performing widening operations, and
 * interleaves when performing narrowing operations. This pass
 * rewrites widenings/narrowings to be explicit in the IR, and
 * attempts to simplify away most of the
 * interleaving/deinterleaving. */
EXPORT Stmt optimize_hexagon(Stmt s, const Target &target);

/** Generate deinterleave or interleave operations, operating on
 * groups of vectors at a time. */
//@{
EXPORT Expr deinterleave(Expr x, const Target &target);
EXPORT Expr interleave(Expr x, const Target &target);
EXPORT bool is_deinterleave(Expr x, const Target &target);
EXPORT bool is_interleave(Expr x, const Target &target);
//@}

}  // namespace Internal
}  // namespace Halide

#endif
