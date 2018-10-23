#ifndef HALIDE_STORAGE_FOLDING_H
#define HALIDE_STORAGE_FOLDING_H

/** \file
 * Defines the lowering optimization pass that reduces large buffers
 * down to smaller circular buffers when possible
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Fold storage of functions if possible. This means reducing one of
 * the dimensions module something for the purpose of storage, if we
 * can prove that this is safe to do. E.g consider:
 *
 \code
 f(x) = ...
 g(x) = f(x-1) + f(x)
 f.store_root().compute_at(g, x);
 \endcode
 *
 * We can store f as a circular buffer of size two, instead of
 * allocating space for all of it.
 */
Stmt storage_folding(Stmt s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
