#ifndef HALIDE_INTERNAL_ADD_PARAMETER_CHECKS_H
#define HALIDE_INTERNAL_ADD_PARAMETER_CHECKS_H

/** \file
 *
 * Defines the lowering pass that adds the assertions that validate
 * scalar parameters.
 */

#include "IR.h"

namespace Halide {

struct Target;

namespace Internal {

/** Insert checks to make sure that all referenced parameters meet
 * their constraints. Also injects any custom requirements provided
 * by the user. */
Stmt add_parameter_checks(const std::vector<Stmt> &requirements, Stmt s, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
