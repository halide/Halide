#ifndef HALIDE_REMOVE_DEAD_ALLOCATIONS_H
#define HALIDE_REMOVE_DEAD_ALLOCATIONS_H

/** \file
 * Defines the lowering pass that removes allocate and free nodes that
 * are not used.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Find Allocate/Free pairs that are never loaded from or stored to,
 *  and remove them from the Stmt. This doesn't touch Realize/Call
 *  nodes and so must be called after storage_flattening.
 */
Stmt remove_dead_allocations(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
