#ifndef FIND_CALLS_H
#define FIND_CALLS_H

/** \file
 *
 * Defines analyses to extract the functions called a function.
 */

#include <map>
#include <string>
#include <vector>

#include "Expr.h"

namespace Halide {
namespace Internal {

class Function;

/** Construct a map from name to Function definition object for all Halide
 *  functions called directly in the definition of the Function f, including
 *  in update definitions, update index expressions, and RDom extents. This map
 *  _does not_ include the Function f, unless it is called recursively by
 *  itself.
 */
std::map<std::string, Function> find_direct_calls(const Function &f);

/** Construct a map from name to Function definition object for all Halide
 *  functions called directly in the definition of the Function f, or
 *  indirectly in those functions' definitions, recursively. This map always
 *  _includes_ the Function f.
 */
std::map<std::string, Function> find_transitive_calls(const Function &f);

/** Find all Functions transitively referenced by any Function in `funcs` and return
 * a map of them. */
std::map<std::string, Function> build_environment(const std::vector<Function> &funcs);

/** Returns the same Functions as build_environment, but returns a vector of
 * Functions instead, where the order is the order in which the Functions were
 * first encountered. This is stable to changes in the names of the Functions. */
std::vector<Function> called_funcs_in_order_found(const std::vector<Function> &funcs);

}  // namespace Internal
}  // namespace Halide

#endif
