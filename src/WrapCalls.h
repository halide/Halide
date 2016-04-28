#ifndef WRAP_CALLS_H
#define WRAP_CALLS_H

/** \file
 *
 * Defines analyses to extract the functions called a function.
 */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Replace every call to wrapped functions (including calls in the RDom's
 * predicates) to call to their wrapper functions. */
std::map<std::string, Function> wrap_func_calls(const std::map<std::string, Function> &env);

}
}

#endif
