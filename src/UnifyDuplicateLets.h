#ifndef HALIDE_UNIFY_DUPLICATE_LETS_H
#define HALIDE_UNIFY_DUPLICATE_LETS_H

/** \file
 * Defines the lowering pass that coalesces redundant let statements
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Find let statements that all define the same value, and make later
 * ones just reuse the symbol names of the earlier ones. */
Stmt unify_duplicate_lets(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
