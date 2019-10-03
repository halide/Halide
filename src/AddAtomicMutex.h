#ifndef HALIDE_ADD_ATOMIC_MUTEX_H
#define HALIDE_ADD_ATOMIC_MUTEX_H

#include "Expr.h"
#include "Function.h"
#include <map>

/** \file
 * Defines the lowering pass that insert mutex allocation code & locks
 * for the atomic nodes that require mutex locks. It also checks whether
 * the atomic operation is valid. It rejects algorithms that has indexing
 * on left-hand-side which references the buffer itself, e.g.
 * f(clamp(f(r), 0, 100)) = f(r) + 1
 * If the SplitTuple pass does not lift out the Provide value as a let 
 * expression. This is confirmed by checking whether the Provide nodes 
 * inside an Atomic node  have let binding values accessing the buffers
 * inside the atomic node. */

namespace Halide {
namespace Internal {

Stmt add_atomic_mutex(Stmt s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
