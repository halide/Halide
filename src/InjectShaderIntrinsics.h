#ifndef HALIDE_INJECT_SHADER_INTRINSICS_H
#define HALIDE_INJECT_SHADER_INTRINSICS_H

/** \file
 * Defines the lowering pass that injects loads and stores
 * for general shader-based target.
 */

#include "IR.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Take a statement with for kernel for loops and turn loads and
 * stores inside the loops into shader load and store
 * intrinsics. */
Stmt inject_shader_intrinsics(Stmt s);
}
}

#endif
