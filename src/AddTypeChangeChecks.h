#ifndef HALIDE_ADD_TYPE_CHANGE_CHECKS_H
#define HALIDE_ADD_TYPE_CHANGE_CHECKS_H

/** \file
 * Defines the lowering pass that injects the overflow-safety preconditions
 * recorded by Func::change_type() into the pipeline's assertion block.
 */

#include <map>
#include <string>

#include "Expr.h"

namespace Halide {
namespace Internal {

class Function;

/** Prepend assertions for any static preconditions that Func::change_type()
 * recorded on the funcs in `env` (that it could not discharge at schedule time,
 * e.g. because a reduction extent was symbolic). Statically-true conditions are
 * dropped. Like the other check passes, the resulting asserts are removed later
 * when the no_asserts target feature is set. */
Stmt add_type_change_checks(const Stmt &s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
