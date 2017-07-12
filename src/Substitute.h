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
EXPORT Expr substitute(const std::string &name, const Expr &replacement, const Expr &expr);

/** Substitute variables with the given name with the replacement
 * expression within stmt. */
EXPORT Stmt substitute(const std::string &name, const Expr &replacement, const Stmt &stmt);

/** Substitute variables with names in the map. */
// @{
EXPORT Expr substitute(const std::map<std::string, Expr> &replacements, const Expr &expr);
EXPORT Stmt substitute(const std::map<std::string, Expr> &replacements, const Stmt &stmt);
// @}

/** Substitute expressions for other expressions. */
// @{
EXPORT Expr substitute(const Expr &find, const Expr &replacement, const Expr &expr);
EXPORT Stmt substitute(const Expr &find, const Expr &replacement, const Stmt &stmt);
// @}

/** Substitutions where the IR may be a general graph (and not just a
 * DAG). */
// @{
Expr graph_substitute(const std::string &name, const Expr &replacement, const Expr &expr);
Stmt graph_substitute(const std::string &name, const Expr &replacement, const Stmt &stmt);
Expr graph_substitute(const Expr &find, const Expr &replacement, const Expr &expr);
Stmt graph_substitute(const Expr &find, const Expr &replacement, const Stmt &stmt);
// @}

/** Substitute in all let Exprs in a piece of IR. Doesn't substitute
 * in let stmts, as this may change the meaning of the IR (e.g. by
 * moving a load after a store). Produces graphs of IR, so don't use
 * non-graph-aware visitors or mutators on it until you've CSE'd the
 * result. */
// @{
Expr substitute_in_all_lets(const Expr &expr);
Stmt substitute_in_all_lets(const Stmt &stmt);
// @}

}
}

#endif
