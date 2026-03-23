#ifndef HALIDE_INTERNAL_SELECT_GPU_API_H
#define HALIDE_INTERNAL_SELECT_GPU_API_H

#include "Expr.h"

/** \file
 * Defines a lowering pass that selects which GPU api to use for each
 * gpu for loop
 */

namespace Halide {

struct Target;

namespace Internal {

/** Replace for loops with GPU_Default device_api with an actual
 * device API depending on what's enabled in the target. Choose the
 * first of the following: opencl, cuda */
Stmt select_gpu_api(const Stmt &s, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
