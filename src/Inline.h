#ifndef HALIDE_INLINE_H
#define HALIDE_INLINE_H

/** \file
 * Methods for replacing calls to functions with their definitions.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Inline a single named function, which must be pure. For a pure function to
 * be inlined, it must not have any specializations (i.e. it can only have one
 * values definition). */
// @{
Stmt inline_function(Stmt s, Function f);
Expr inline_function(Expr e, Function f);
void inline_function(Function caller, Function f);
// @}

/** Check if the schedule of an inlined function is legal, throwing an error
 * if it is not. */
void validate_schedule_inlined_function(Function f);

}  // namespace Internal
}  // namespace Halide

#endif
