#ifndef HALIDE_STRICTIFY_FLOAT_H
#define HALIDE_STRICTIFY_FLOAT_H

/** \file
 * Defines a lowering pass to make all floating-point strict for all top-level Exprs.
 */

#include <map>

#include "Function.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Propagate strict_float intrinisics such that they immediately wrap
 * all floating-point expressions. This makes the IR nodes context
 * independent.  If the Target::StrictFloat flag is specified in
 * target, starts in strict_float mode so all floating-point type
 * Exprs in the compilation will be marked with strict_float. Returns
 * whether any strict floating-point is used in any function in the
 * passed in env.
 */
bool strictify_float(std::map<std::string, Function> &env, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
