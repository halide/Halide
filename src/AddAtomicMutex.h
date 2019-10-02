#ifndef HALIDE_ADD_ATOMIC_MUTEX_H
#define HALIDE_ADD_ATOMIC_MUTEX_H

#include "Expr.h"
#include "Function.h"
#include <map>

/** \file
 * Defines the lowering pass that insert mutex allocation code & locks
 * for the atomic nodes that require mutex locks. If the SplitTuple pass
 * does not lift out the Provide value as a let expression. This is
 * confirmed by checking whether the Provide nodes inside an Atomic node 
 * have let binding values accessing the buffers inside the atomic node. */

namespace Halide {
namespace Internal {

Stmt add_atomic_mutex(Stmt s, const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
