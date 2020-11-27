#ifndef HALIDE_FIND_VECTOR_REDUCTIONS_H
#define HALIDE_FIND_VECTOR_REDUCTIONS_H

/** \file
 * Defines the lowering pass that unrolls loops marked as such
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Try to rewrite expressions as vector reductions. */
Stmt find_vector_reductions(Stmt);
Expr find_vector_reductions(Expr);

}  // namespace Internal
}  // namespace Halide

#endif
