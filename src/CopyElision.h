#ifndef HALIDE_COPY_ELISION
#define HALIDE_COPY_ELISION

#include <map>

#include "IR.h"

/** \file
 * Defines a lowering pass that eliminates unnecessary copies.
 */

namespace Halide {
namespace Internal {

/** Return all pairs of functions which operation only involves pointwise copy
  * of another function and the function from which it copies from. Ignore
  * functions that have updates or are extern functions.
  * Result: {cons -> prod}
  */
std::map<std::string, std::string> get_pointwise_copies(const std::map<std::string, Function> &env);

std::string print_function(const Function &f);

void copy_elision_test();

}  // namespace Internal
}  // namespace Halide

#endif
