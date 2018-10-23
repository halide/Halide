#ifndef __HALIDE_VARYING_ATTRIBUTES__H
#define __HALIDE_VARYING_ATTRIBUTES__H

/** \file
 * This file contains functions that detect expressions in a GLSL scheduled
 * function that may be evaluated per vertex and interpolated across the domain
 * instead of being evaluated at each pixel location across the image.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** find_linear_expressions(Stmt s) identifies expressions that may be moved
 * out of the generated fragment shader into a varying attribute. These
 * expressions are tagged by wrapping them in a glsl_varying intrinsic
 */
Stmt find_linear_expressions(Stmt s);

/** Compute a set of 2D mesh coordinates based on the behavior of varying
 * attribute expressions contained within a GLSL scheduled for loop. This
 * method is called during lowering to extract varying attribute
 * expressions and generate code to evalue them at each mesh vertex
 * location. The operation is performed on the host before the draw call
 * to invoke the shader
 */
Stmt setup_gpu_vertex_buffer(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
