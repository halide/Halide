#ifndef HALIDE_WRAP_CALLS_H
#define HALIDE_WRAP_CALLS_H

/** \file
 *
 * Defines pass to replace calls to wrapped Functions with their wrappers.
 */

#include "Util.h"

namespace Halide {
namespace Internal {

class Function;

/** Replace every call to wrapped Functions in the Functions' definitions with
 * call to their wrapper functions. */
StringMap<Function> wrap_func_calls(const StringMap<Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
