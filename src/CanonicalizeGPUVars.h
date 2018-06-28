#ifndef HALIDE_CANONICALIZE_GPU_VARS_H
#define HALIDE_CANONICALIZE_GPU_VARS_H

/** \file
 * Defines the lowering pass that canonicalize the GPU var names over.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Canonicalize GPU var names into some pre-determined block/thread names
 * (i.e. __block_id_x, __thread_id_x, etc.). The x/y/z/w order is determined
 * by the nesting order: innermost is assigned to x and so on. */
Stmt canonicalize_gpu_vars(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
