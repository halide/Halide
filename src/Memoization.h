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
                        const std::string &name);

}
}

#endif
