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
 * predicates) to call to their wrapper functions. Create a deep-copy of the
 * Function if mutated. */
std::pair<std::vector<Function>, std::map<std::string, Function>> wrap_func_calls(
	const std::vector<Function> &outputs, const std::map<std::string, Function> &env);

}
}

#endif
