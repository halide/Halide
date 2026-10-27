#ifndef OPTIMIZE_SHUFFLES_H
#define OPTIMIZE_SHUFFLES_H

/** \file
 * Defines lowering passes that replace indirect
 * loads and small table lookups with dynamic_shuffle
 * intrinsics where possible.
 */

#include "Expr.h"

namespace Halide {

struct Target;

namespace Internal {

/* Replace indirect loads with dynamic_shuffle intrinsics where
possible. */
Stmt optimize_shuffles(Stmt s, int lut_alignment);

/** Largest table of scalar type t that the target can profitably index with
 * dynamic_shuffle, or zero if no table-lookup instruction is supported.
 * Scalable-vector ARM targets are conservatively excluded. */
int max_dynamic_shuffle_table_size(const Type &t, const Target &target);

/** Replace vectorized constant muxes and bounded loads from embedded buffers
 * or fixed-size internal allocations with dynamic_shuffle(table, index, 0,
 * n - 1), with a uint8 index in [0, n - 1]. Out-of-range mux indices select
 * the last entry. Loads cover only a proven-safe dense span; buffer contents
 * are not assumed constant. Run after GPU offload and before closure lowering
 * so embedded buffers and enclosing allocations are still visible. */
Stmt optimize_small_table_lookups(const Stmt &s, const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
