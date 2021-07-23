#ifndef HALIDE_PREFETCH_DIRECTIVE_H
#define HALIDE_PREFETCH_DIRECTIVE_H

/** \file
 * Defines the PrefetchDirective struct
 */

#include <string>

#include "Expr.h"
#include "Parameter.h"

namespace Halide {

/** Different ways to handle accesses outside the original extents in a prefetch. */
enum class PrefetchBoundStrategy {
    /** Clamp the prefetched exprs by intersecting the prefetched region with
     * the original extents. This may make the exprs of the prefetched region
     * more complicated. */
    Clamp,

    /** Guard the prefetch with if-guards that ignores the prefetch if
     * any of the prefetched region ever goes beyond the original extents
     * (i.e. all or nothing). */
    GuardWithIf,

    /** Leave the prefetched exprs as-is (no if-guards around the prefetch
     * and no intersecting with the original extents). This makes the prefetch
     * exprs simpler but this may cause prefetching of the region outside the original
     * extents. This is good if prefetch won't fault when accessing region
     * outside the original extents. */
    NonFaulting
};

namespace Internal {

struct PrefetchDirective {
    std::string name;
    std::string at;         // the loop in which to do the prefetch
    std::string from;       // the loop-var to use as the base for prefetching. It must be nested outside loop_var (or be equal to loop_var).
    Expr offset;            // 'fetch_var + offset' will determine the bounds being prefetched.
    PrefetchBoundStrategy strategy;
    // If it's a prefetch load from an image parameter, this points to that.
    Parameter param;
};

}  // namespace Internal

}  // namespace Halide

#endif  // HALIDE_PREFETCH_DIRECTIVE_H
