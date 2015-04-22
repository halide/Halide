#ifndef HALIDE_INJECT_IMAGE_INTRINSICS_H
#define HALIDE_INJECT_IMAGE_INTRINSICS_H

/** \file
 * Defines the lowering pass that injects coordinate-based loads and stores
 * for general coordinate/texture-based target.
 */

#include "IR.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Take a statement with for kernel for loops and turn loads and
 * stores inside the loops into coordinate-based load and store
 * intrinsics. */
Stmt inject_image_intrinsics(Stmt s);
}
}

#endif
