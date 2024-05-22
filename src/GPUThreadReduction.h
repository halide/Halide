#ifndef HALIDE_GPU_THREAD_REDUCTION_H
#define HALIDE_GPU_THREAD_REDUCTION_H

/** \file
 * Defines the lowering passes that convert gpu thread reduction
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** convert sum to gpu thread reduction */
Stmt gpu_thread_reduction(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
