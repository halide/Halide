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

    /** Leave the prefetched exprs as are (no if-guards around the prefetch
     * and no intersecting with the original extents). This makes the prefetch
     * exprs simpler but this may cause prefetching of region outside the original
     * extents. This is good if prefetch won't fault when accessing region
     * outside the original extents. */
    NonFaulting
};

namespace Internal {

struct PrefetchDirective {
    std::string name;
    std::string var;
    Expr offset;
    PrefetchBoundStrategy strategy;
    // If it's a prefetch load from an image parameter, this points to that.
    Parameter param;
};

}  // namespace Internal

}  // namespace Halide

#endif  // HALIDE_PREFETCH_DIRECTIVE_H
