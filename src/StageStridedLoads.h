#ifndef HALIDE_INTERNAL_STAGE_STRIDED_LOADS_H
#define HALIDE_INTERNAL_STAGE_STRIDED_LOADS_H

/** \file
 *
 * Defines the compiler pass that converts strided loads into dense loads
 * followed by shuffles.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Convert all unpredicated strided loads in a Stmt into dense loads followed
 * by shuffles.
 *
 * For a stride of two, the trick is to do a dense load of twice the size, and
 * then extract either the even or odd lanes. This was previously done in
 * codegen, where it was challenging, because it's not easy to know there if
 * it's safe to do the double-sized load, as it either loads one element beyond
 * or before the original load. We used the alignment of the ramp base to try to
 * tell if it was safe to shift backwards, and we added padding to internal
 * allocations so that for those at least it was safe to shift
 * forwards. Unfortunately the alignment of the ramp base is usually unknown if
 * you don't know anything about the strides of the input, and adding padding to
 * allocations was a serious wart in our memory allocators.
 *
 * This pass instead actively looks for evidence elsewhere in the Stmt (at some
 * location which definitely executes whenever the load being transformed
 * executes) that it's safe to read further forwards or backwards in memory. The
 * evidence is in the form of a load at the same base address with a different
 * constant offset. It also clusters groups of these loads so that they do the
 * same dense load and extract the appropriate slice of lanes. If it fails to
 * find any evidence, for loads from external buffers it does two overlapping
 * half-sized dense loads and shuffles out the desired lanes, and for loads from
 * internal allocations it adds padding to the allocation explicitly, by setting
 * the padding field on Allocate nodes.
 */
Stmt stage_strided_loads(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
