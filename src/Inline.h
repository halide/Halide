#ifndef HALIDE_INLINE_H
#define HALIDE_INLINE_H

/** \file
 * Methods for replacing calls to functions with their definitions.
 */

#include "Expr.h"
#include "Util.h"  // for HALIDE_EXPORT_FOR_PLUGINS

namespace Halide {
namespace Internal {

class Function;

/** Inline a single named function, which must be pure. For a pure function to
 * be inlined, it must not have any specializations (i.e. it can only have one
 * values definition). */
// @{
HALIDE_EXPORT_FOR_PLUGINS Stmt inline_function(Stmt s, const Function &f);
HALIDE_EXPORT_FOR_PLUGINS Expr inline_function(Expr e, const Function &f);
HALIDE_EXPORT_FOR_PLUGINS void inline_function(Function caller, const Function &f);
// @}

/** Check if the schedule of an inlined function is legal, throwing an error
 * if it is not. */
void validate_schedule_inlined_function(Function f);

}  // namespace Internal
}  // namespace Halide

#endif
