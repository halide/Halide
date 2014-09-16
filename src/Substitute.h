#ifndef HALIDE_SUBSTITUTE_H
#define HALIDE_SUBSTITUTE_H

/** \file
 *
 * Defines methods for substituting out variables in expressions and
 * statements. */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Substitute variables with the given name with the replacement
 * expression within expr. This is a dangerous thing to do if variable
 * names have not been uniquified. While it won't traverse inside let
 * statements with the same name as the first argument, moving a piece
 * of syntax around can change its meaning, because it can cross lets
 * that redefine variable names that it includes references to. */
EXPORT Expr substitute(std::string name, Expr replacement, Expr expr);

/** Substitute variables with the given name with the replacement
 * expression within stmt. */
EXPORT Stmt substitute(std::string name, Expr replacement, Stmt stmt);

/** Substitute variables with names in the map. */
// @{
EXPORT Expr substitute(const std::map<std::string, Expr> &replacements, Expr expr);
EXPORT Stmt substitute(const std::map<std::string, Expr> &replacements, Stmt stmt);
// @}

/** Substitute expressions for other expressions. */
// @{
EXPORT Expr substitute(Expr find, Expr replacement, Expr expr);
EXPORT Stmt substitute(Expr find, Expr replacement, Stmt stmt);
// @}

}
}

#endif
