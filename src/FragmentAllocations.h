#ifndef HALIDE_FRAGMENT_ALLOCATIONS_H
#define HALIDE_FRAGMENT_ALLOCATIONS_H

/** \file
 * Defines the lowering pass that breaks up non-scalar constant-sized
 * allocations accessed with constant indices into a set of scalar
 * ones. */

#include "IR.h"

namespace Halide {
namespace Internal {

/** For all allocate nodes of a constant size, if all access to them
 * is at constant indices, break the allocation into a bunch of scalar
 * allocations instead. This is important for local allocations inside
 * PTX kernels. */
Stmt fragment_allocations(Stmt s);

/** Call fragment_allocations on the body of each cuda thread loop */
Stmt fragment_cuda_local_allocations(Stmt s);

}
}

#endif
