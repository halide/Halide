#ifndef HALIDE_WRAP_CALLS_H
#define HALIDE_WRAP_CALLS_H

/** \file
 *
 * Defines pass to replace calls to wrapped Functions with their wrappers.
 */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Replace every call to wrapped Functions in the Functions' definitions with
  * call to their wrapper functions. */
std::map<std::string, Function> wrap_func_calls(const std::map<std::string, Function> &env);

}
}

#endif