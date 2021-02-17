#ifndef HALIDE_EXTRACT_TILE_OPERATIONS_H
#define HALIDE_EXTRACT_TILE_OPERATIONS_H

/** \file
 * Defines the lowering pass that injects calls to tile intrinsics that support
 * AMX instructions.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** TODO */
Stmt extract_tile_operations(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
