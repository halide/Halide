#ifndef HALIDE_SUBSTITUTE_H
#define HALIDE_SUBSTITUTE_H

/** \file
 *
 * Defines methods for substituting out variables in expressions and
 * statements. */

#include "IRMutator.h"

namespace Halide { 
namespace Internal {

/** Substitute variables with the given name with the replacement
 * expression within expr. This is a dangerous thing to do if variable
 * names have not been uniquified. While it won't traverse inside let
 * statements with the same name as the first argument, moving a piece
 * of syntax around can change its meaning, because it can cross lets
 * that redefine variable names that it includes references to. */
Expr substitute(std::string name, Expr replacement, Expr expr);

/** Substitute variables with the given name with the replacement
 * expression within stmt. */
Stmt substitute(std::string name, Expr replacement, Stmt stmt);

}
}

#endif
