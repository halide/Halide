#ifndef HALIDE_REMOVE_ATOMIC_MUTEX_LOCKS_H
#define HALIDE_REMOVE_ATOMIC_MUTEX_LOCKS_H

#include "Expr.h"
#include "Function.h"
#include <map>

/** \file
 * Defines the lowering pass that optimizes out the mutex lock of an
 * atomic node, if the SplitTuple pass does not lift out the Provide
 * value as a let expression. This is confirmed by checking whether
 * the Provide nodes inside an Atomic node has a value of a variable,
 * where the name of the variable is [name of the Provide node].value. 
 * If we detect that the Atomic node does not require a mutex lock,
 * we remove the mutex access of the atomic node, the corresponding
 * mutex allocations and frees. We don't rely on dead allocation
 * removal because it won't remove the mutex initialization code.
 * Please do this pass immediately after the SplitTuple pass. */

namespace Halide {
namespace Internal {

/** Remove unnecessary mutex locks in atomic nodes. */
Stmt remove_atomic_mutex_locks(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
