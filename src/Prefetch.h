#ifndef HALIDE_PREFETCH_H
#define HALIDE_PREFETCH_H

/** \file
 * Defines the lowering pass that injects prefetch calls when prefetching
 * appears in the schedule.
 */

#include <map>

#include "IR.h"
#include "Schedule.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Inject placeholder prefetches to 's'. This placholder prefetch
  * does not have explicit region to be prefetched yet. It will be computed
  * during call to \ref inject_prefetch. */
Stmt inject_placeholder_prefetch(Stmt s, const std::map<std::string, Function> &env,
                                 const std::string &prefix,
                                 const std::vector<PrefetchDirective> &prefetches);
/** Compute the actual region to be prefetched and place it to the
  * placholder prefetch. Wrap the prefetch call with condition when
  * applicable. */
Stmt inject_prefetch(Stmt s, const std::map<std::string, Function> &env);

/** Reduce a multi-dimensional prefetch into a prefetch of lower dimension
 * (max dimension of the prefetch is specified by target architecture).
 * This keeps the 'max_dim' innermost dimensions and adds loops for the rest
 * of the dimensions. If maximum prefetched-byte-size is specified (depending
 * on the architecture), this also adds an outer loops that tile the prefetches. */
Stmt reduce_prefetch_dimension(Stmt stmt, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
