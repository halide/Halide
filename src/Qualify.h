#ifndef HALIDE_QUALIFY_H
#define HALIDE_QUALIFY_H

/** \file
 *
 * Defines methods for prefixing names in an expression with a prefix string.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Prefix all variable names in the given expression with the prefix string. */
Expr qualify(const std::string &prefix, Expr value);

}  // namespace Internal
}  // namespace Halide

#endif
