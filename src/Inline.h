#ifndef HALIDE_INLINE_H
#define HALIDE_INLINE_H

/** \file
 * Methods for replacing calls to functions with their definitions.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Inline a single named function, which must be pure. For a pure function to
 * be inlined, it must not have any specializations (i.e. it can only have one
 * values definition). */
// @{
Stmt inline_function(Stmt, Function);
Expr inline_function(Expr, Function);
// @}

}
}


#endif
