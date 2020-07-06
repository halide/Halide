#ifndef HALIDE_VECTORIZE_LOOPS_H
#define HALIDE_VECTORIZE_LOOPS_H

/** \file
 * Defines the lowering pass that vectorizes loops marked as such
 */

#include "Expr.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Take a statement with for loops marked for vectorization, and turn
 * them into single statements that operate on vectors. The loops in
 * question must have constant extent.
 */
Stmt vectorize_loops(const Stmt &s, const std::map<std::string, Function> &env, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
