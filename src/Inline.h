#ifndef HALIDE_INLINE_H
#define HALIDE_INLINE_H


#include "IR.h"

/** \file
 * Methods for replacing calls to functions with their definitions.
 */

namespace Halide {
namespace Internal {

/** Inline a single named function, which must be pure. */
// @{
Stmt inline_function(Stmt, Function);
Expr inline_function(Expr, Function);
// @}

}
}


#endif
