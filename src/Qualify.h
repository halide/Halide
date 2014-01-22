#ifndef QUALIFY_H
#define QUALIFY_H

#include "IR.h"

/** \file
 *
 * Defines methods for prefixing names in an expression with a prefix string.
 */

namespace Halide {
namespace Internal {

/** Prefix all variable names in the given expression with the prefix string. */
Expr qualify(const std::string &prefix, Expr value);

}
}


#endif
