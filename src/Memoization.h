#ifndef HALIDE_INTERNAL_CACHING_H
#define HALIDE_INTERNAL_CACHING_H

/** \file
 *
 * Defines the interface to the pass that injects support for
 * compute_cached roots.
 */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Transform pipeline calls for Funcs scheduled with memoize to do a
 *  lookup call to the runtime cache implementation, and if there is a
 *  miss, compute the results and call the runtime to store it back to
 *  the cache.
 *  Should leave non-memoized Funcs unchanged.
 */
Stmt inject_memoization(Stmt s, const std::map<std::string, Function> &env,
                        const std::string &name,
                        const std::vector<Function> &outputs);

/** This should be called after Storage Flattening has added Allocation
 *  IR nodes. It connects the memoization cache lookups to the Allocations
 *  so they point to the buffers from the memoization cache and those buffers
 *  are released when no longer used.
 *  Should not affect allocations for non-memoized Funcs.
 */
Stmt rewrite_memoized_allocations(Stmt s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
