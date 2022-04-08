#ifndef HALIDE_PREFETCH_H
#define HALIDE_PREFETCH_H

/** \file
 * Defines the lowering pass that injects prefetch calls when prefetching
 * appears in the schedule.
 */

#include <map>
#include <string>
#include <vector>

namespace Halide {

struct Target;

namespace Internal {

class Function;
struct PrefetchDirective;
struct Stmt;

/** Inject placeholder prefetches to 's'. This placholder prefetch
 * does not have explicit region to be prefetched yet. It will be computed
 * during call to \ref inject_prefetch. */
Stmt inject_placeholder_prefetch(const Stmt &s, const std::map<std::string, Function> &env,
                                 const std::string &prefix,
                                 const std::vector<PrefetchDirective> &prefetches);
/** Compute the actual region to be prefetched and place it to the
 * placholder prefetch. Wrap the prefetch call with condition when
 * applicable. */
Stmt inject_prefetch(const Stmt &s, const std::map<std::string, Function> &env);

/** Reduce a multi-dimensional prefetch into a prefetch of lower dimension
 * (max dimension of the prefetch is specified by target architecture).
 * This keeps the 'max_dim' innermost dimensions and adds loops for the rest
 * of the dimensions. If maximum prefetched-byte-size is specified (depending
 * on the architecture), this also adds an outer loops that tile the prefetches. */
Stmt reduce_prefetch_dimension(Stmt stmt, const Target &t);

/** Hoist all the prefetches in a Block to the beginning of the Block.
 * This generally only happens when a loop with prefetches is unrolled;
 * in some cases, LLVM's code generation can be suboptimal (unnecessary register spills)
 * when prefetches are scattered through the loop. Hoisting to the top of the
 * loop is a good way to mitigate this, at the cost of the prefetch calls possibly
 * being less useful due to distance from use point. (This is a bit experimental
 * and may need revisiting.) See also https://bugs.llvm.org/show_bug.cgi?id=51172 */
Stmt hoist_prefetches(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
