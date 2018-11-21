#ifndef HALIDE_UNIQUIFY_VARIABLE_NAMES
#define HALIDE_UNIQUIFY_VARIABLE_NAMES

/** \file
 * Defines the lowering pass that renames all variables to have unique names.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Modify a statement so that every internally-defined variable name
 * is unique. This lets later passes assume syntactic equivalence is
 * semantic equivalence. */
Stmt uniquify_variable_names(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
