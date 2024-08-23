#ifndef HALIDE_CANONICALIZE_GPU_VARS_H
#define HALIDE_CANONICALIZE_GPU_VARS_H

/** \file
 * Defines the lowering pass that canonicalize the GPU var names over.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Canonicalize GPU var names into some pre-determined block/thread names
 * (i.e. __block_id_x, __thread_id_x, etc.). The x/y/z/w order is determined
 * by the nesting order: innermost is assigned to x and so on. */
Stmt canonicalize_gpu_vars(Stmt s);

/** Names for the thread and block id variables. Includes the leading
 * dot. Indexed from inside out, so 0 gives you the innermost loop. */
// @{
const std::string &gpu_thread_name(int index);
const std::string &gpu_block_name(int index);
// @}

}  // namespace Internal
}  // namespace Halide

#endif
