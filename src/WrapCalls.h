#ifndef WRAP_CALLS_H
#define WRAP_CALLS_H

/** \file
 *
 * Defines pass to replace calls to wrapped Functions with their wrappers.
 */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Create deep-copies of all Functions in 'env' and replace every call to
 * wrapped Functions in the Functions' definitions with call to their wrapper
 * functions. */
std::pair<std::vector<Function>, std::map<std::string, Function>> wrap_func_calls(
	const std::vector<Function> &outputs, const std::map<std::string, Function> &env);

}
}

#endif
