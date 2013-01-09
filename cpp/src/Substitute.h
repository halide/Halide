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
 * expression within expr */
Expr substitute(std::string name, Expr replacement, Expr expr);

/** Substitute variables with the given name with the replacement
 * expression within stmt */
Stmt substitute(std::string name, Expr replacement, Stmt stmt);

}
}

#endif
