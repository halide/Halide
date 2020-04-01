#ifndef HALIDE_UNSAFE_PROMISES_H
#define HALIDE_UNSAFE_PROMISES_H

#include "Expr.h"
/** \file
 * Defines the lowering pass that removes unsafe promises
 */

namespace Halide {
struct Target;

namespace Internal {

/** Lower all unsafe promises into either assertions or unchecked
    code, depending on the target. */
Stmt lower_unsafe_promises(const Stmt &s, const Target &t);

/** Lower all safe promises by just stripping them. This is a good
 * idea once no more lowering stages are going to use
 * boxes_touched. */
Stmt lower_safe_promises(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
