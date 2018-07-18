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
Stmt optimize_hexagon_shuffles(Stmt s, int lut_alignment);

/** Generate vtmpy instruction if possible */
Stmt vtmpy_generator(Stmt s);

/* Generate Vscatter-Vgather instructions on Hexagon using VTCM memory.
 * vtcm_base_string is the name for the base pointer to VTCM allocated
 * memory. vtcm_size is the max amount of vtcm_memory we can consume. */
Stmt scatter_gather_generator(Stmt s, std::string vtcm_base_string,
                                     int vtcm_size, Target t);

/** Hexagon deinterleaves when performing widening operations, and
 * interleaves when performing narrowing operations. This pass
 * rewrites widenings/narrowings to be explicit in the IR, and
 * attempts to simplify away most of the
 * interleaving/deinterleaving. */
Stmt optimize_hexagon_instructions(Stmt s, Target t);

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
