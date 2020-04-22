#ifndef HALIDE_INTERNAL_ADD_IMAGE_CHECKS_H
#define HALIDE_INTERNAL_ADD_IMAGE_CHECKS_H

/** \file
 *
 * Defines the lowering pass that adds the assertions that validate
 * input and output buffers.
 */
#include <map>
#include <string>
#include <vector>

#include "Bounds.h"
#include "Expr.h"

#include <map>

namespace Halide {

struct Target;

namespace Internal {

class Function;

/** Insert checks to make sure a statement doesn't read out of bounds
 * on inputs or outputs, and that the inputs and outputs conform to
 * the format required (e.g. stride.0 must be 1).
 */
Stmt add_image_checks(const Stmt &s,
                      const std::vector<Function> &outputs,
                      const Target &t,
                      const std::vector<std::string> &order,
                      const std::map<std::string, Function> &env,
                      const FuncValueBounds &fb,
                      bool will_inject_host_copies);

}  // namespace Internal
}  // namespace Halide

#endif
