#ifndef HALIDE_EXPR_USES_VAR_H
#define HALIDE_EXPR_USES_VAR_H

/** \file
 * Defines a method to determine if an expression depends on some variables.
 */

#include "IR.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Test if an expression references the given variable. */
bool expr_uses_var(Expr e, const std::string &v);

/** Test if an expression references any of the variables in a scope. */
bool expr_uses_vars(Expr e, const Scope<int> &s);

}
}

#endif
