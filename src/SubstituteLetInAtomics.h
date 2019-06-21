#ifndef HALIDE_SUBSTITUTE_LET_IN_ATOMICS_H
#define HALIDE_SUBSTITUTE_LET_IN_ATOMICS_H

#include "Expr.h"
#include "Function.h"
#include <map>

/** \file
 * Defines the lowering pass that substitute let expressions or statements 
 * inside atomic nodes that does not employ mutex lock.
 * For example, consider the following case:
 *
 * f(input(r)) = Tuple(0, 0);
 * f(input(r)) += Tuple(1, 2);
 * f.update().atomic().parallel(r);
 *
 * The split_tuples lowering pass would lower the update into:
 * let v0 = f(input(r))[0]
 * let v1 = f(input(r))[1]
 * f(input(r))[0] = v0 + 1
 * f(input(r))[1] = v1 + 1
 * 
 * This breaks the atomic operations if we do not lock the whole region. Fortunately,
 * In many case it is possible to substitute the let statements in without changing
 * the meaning of the statement. This pass checks this, substitutes when possible and
 * necessary, and triggers assertions if it fails to do so.
 * This pass only searches for Store and Load, so call it after the storage_flattening pass. */

namespace Halide {
namespace Internal {

/** Substitutes let expressions or statements in in atomic nodes if necessary. Triggers assertions
    if fail to do so. */
Stmt substitute_let_in_atomics(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
