#ifndef HALIDE_SYNCTHREADS_H
#define HALIDE_SYNCTHREADS_H

/** \file
 * Defines the lowering pass that does thread synchronization between
 * blocks on the gpu.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement with for kernel for loops add thread
 * synchronization primitives between statements at the block level. */
Stmt inject_syncthreads(Stmt s);

}
}

#endif
