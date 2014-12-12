#ifndef HALIDE_REMOVE_TRIVIAL_ALLOCATIONS_H
#define HALIDE_REMOVE_TRIVIAL_ALLOCATIONS_H

/** \file
 * Defines the lowering pass that removes allocate and free nodes that are not used.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Find Allocate/Free pairs that are never loaded from or stored to, and remove them from the Stmt.
 */
Stmt remove_trivial_allocations(Stmt s);

}
}

#endif
