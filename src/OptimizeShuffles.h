#ifndef OPTIMIZE_SHUFFLES_H
#define OPTIMIZE_SHUFFLES_H

/** \file
 * Defines lowering passes that replace indirect
 * loads and small table lookups with dynamic_shuffle
 * intrinsics where possible.
 */

#include "Expr.h"

#include <functional>

namespace Halide {
namespace Internal {

/* Replace indirect loads with dynamic_shuffle intrinsics where
possible. */
Stmt optimize_shuffles(Stmt s, int lut_alignment);

/** Replace vectorized lookups into small tables whose size is known at
 * compile time with dynamic_shuffle intrinsics, which backends can lower to
 * register table-lookup instructions (e.g. ARM tbl, x86 pshufb, WebAssembly
 * i8x16.swizzle). Two forms are recognized:
 *
 * - mux(index, {c0, c1, ..., cn-1}) where every value is a constant. The
 *   table is materialized as a constant vector. Out-of-range indices are
 *   clamped to n-1, matching the semantics of mux.
 *
 * - Loads from a Buffer embedded in the pipeline (i.e. a Load with a defined
 *   image and no Parameter) whose index can be proven to lie in a range of
 *   at most n elements that is entirely inside the buffer's allocation. The
 *   table is a dense vector load of that range. The contents are not assumed
 *   to be constant, but the shape of an embedded buffer is, so no padding is
 *   required and no out-of-bounds reads are introduced.
 *
 * max_table_size(t) should return the largest number of elements of the
 * scalar type t that the target can profitably use as a dynamic_shuffle
 * table, or zero if lookups into tables of that type should be left
 * alone. The resulting dynamic_shuffle calls have the form
 * dynamic_shuffle(table, index, 0, n - 1) where index is a vector of uint8
 * known to be in [0, n - 1]. */
Stmt optimize_small_table_lookups(const Stmt &s,
                                  const std::function<int(const Type &)> &max_table_size);

}  // namespace Internal
}  // namespace Halide

#endif
