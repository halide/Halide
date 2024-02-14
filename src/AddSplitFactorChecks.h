#ifndef HALIDE_INTERNAL_ADD_SPLIT_FACTOR_CHECKS_H
#define HALIDE_INTERNAL_ADD_SPLIT_FACTOR_CHECKS_H

/** \file
 *
 * Defines the lowering pass that adds the assertions that all split factors are
 * strictly positive.
 */
#include <map>

#include "Expr.h"

namespace Halide {
namespace Internal {

class Function;

/** Insert checks that all split factors that depend on scalar parameters are
 * strictly positive. */
Stmt add_split_factor_checks(const Stmt &s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
