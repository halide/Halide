#ifndef HALIDE_CODEGEN_H
#define HALIDE_CODEGEN_H

/** \file
 * Support for all code generator types in Halide. Currently a
 * placeholder with just one utility function.
 */

#include <string>

namespace Halide {
namespace Internal {

/** Which built-in functions require a user-context first argument? */
bool function_takes_user_context(const std::string &name);

}
}

#endif
