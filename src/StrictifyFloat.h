#ifndef HALIDE_STRICTIFY_FLOAT_H
#define HALIDE_STRICTIFY_FLOAT_H

/** \file
 * Defines a lowering pass to make all floating-point strict for all top-level Exprs.
 */

#include <map>
#include <string>

namespace Halide {

struct Target;
struct Expr;

namespace Internal {

class Function;
struct Call;

/** Replace all rounding floating point ops and floating point ops that need to
 * handle nan and inf differently with strict float intrinsics. */
Expr strictify_float(const Expr &e);

/** Replace a strict float intrinsic with its non-strict equivalent. Non-recursive. */
Expr unstrictify_float(const Call *op);

/** Replace all of the strict float intrinsics with its non-strict equivalent in a given expression. */
Expr unstrictify_all(const Expr &e);

/** If the StrictFloat target feature is set, replace add, sub, mul, div, etc
 * operations with strict float intrinsics for all Funcs in the environment. If
 * StrictFloat is not set does nothing. Returns whether or not there's any usage
 * of strict float intrinsics or if the target flag is set (i.e. returns whether
 * or not the rest of lowering and codegen needs to worry about floating point
 * strictness). */
bool strictify_float(std::map<std::string, Function> &env, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
