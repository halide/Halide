#ifndef HALIDE_CPLUSPLUS_MANGLE_H
#define HALIDE_CPLUSPLUS_MANGLE_H

/** \file
 *
 * A simple function to get a C++ mangled function name for a Call operator.
 */

#include <string>
#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Return the mangled C++ name for a function corresponding to this
 * Call. The target parameter is used to decide on the C++
 * ABI/mangling style to use.
 */
std::string cplusplus_mangled_name(const Call *op, const Target &target);

}
}

#endif
