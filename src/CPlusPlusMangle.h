#ifndef HALIDE_CPLUSPLUS_MANGLE_H
#define HALIDE_CPLUSPLUS_MANGLE_H

/** \file
 *
 * A simple function to get a C++ mangled function name for a function.
 */

#include "IR.h"
#include "Target.h"
#include <string>

namespace Halide {
namespace Internal {

/** Return the mangled C++ name for a function.
 * The target parameter is used to decide on the C++
 * ABI/mangling style to use.
 */
std::string cplusplus_function_mangled_name(const std::string &name,
                                            const std::vector<std::string> &namespaces,
                                            Type return_type,
                                            const std::vector<ExternFuncArgument> &args,
                                            const Target &target);

void cplusplus_mangle_test();

}  // namespace Internal

}  // namespace Halide

#endif
