#ifndef HALIDE_INTERNAL_LEGALIZE_VECTORS_H
#define HALIDE_INTERNAL_LEGALIZE_VECTORS_H

#include "Expr.h"

/** \file
 * Defines a lowering pass that legalizes vectorized expressions
 * to have a maximal lane count.
 */

namespace Halide {
namespace Internal {

Stmt legalize_vectors(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
