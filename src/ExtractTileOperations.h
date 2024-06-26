#ifndef HALIDE_EXTRACT_TILE_OPERATIONS_H
#define HALIDE_EXTRACT_TILE_OPERATIONS_H

/** \file
 * Defines the lowering pass that injects calls to tile intrinsics that support
 * AMX instructions.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Rewrite any AMX tile operations that have been stored in the AMXTile memory
 * type as intrinsic calls, to be used in the X86 backend. */
Stmt extract_tile_operations(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
