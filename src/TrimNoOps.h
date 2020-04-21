#ifndef TRIM_NO_OPS_H
#define TRIM_NO_OPS_H

/** \file
 * Defines a lowering pass that truncates loops to the region over
 * which they actually do something.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Truncate loop bounds to the region over which they actually do
 * something. For examples see test/correctness/trim_no_ops.cpp */
Stmt trim_no_ops(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
