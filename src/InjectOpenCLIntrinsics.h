#ifndef HALIDE_INJECT_OPENCL_INTRINSICS_H
#define HALIDE_INJECT_OPENCL_INTRINSICS_H

/** \file
 * Defines the lowering pass that injects image-based loads and stores
 * for general image/texture-based target.
 */

#include "IR.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Take a statement with for kernel for loops and turn loads and
 * stores inside the loops into read_image and write_image
 * intrinsics. */
Stmt inject_opencl_intrinsics(Stmt s);
}
}

#endif
