#ifndef HALIDE_INJECT_OPENGL_INTRINSICS_H
#define HALIDE_INJECT_OPENGL_INTRINSICS_H

/** \file
 * Defines the lowering pass that injects texture loads and texture
 * stores for opengl.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement with for kernel for loops and turn loads and
 * stores inside the loops into OpenGL texture load and store
 * intrinsics. Should only be run when the OpenGL target is active. */
Stmt inject_opengl_intrinsics(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
