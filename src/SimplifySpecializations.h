#ifndef SIMPLIFY_SPECIALIZATIONS_H
#define SIMPLIFY_SPECIALIZATIONS_H

/** \file
 *
 * Defines pass that try to simplify the RHS/LHS of a function's definition
 * based on its specializations.
 */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Try to simplify the RHS/LHS of a function's definition based on its
 * specializations. */
void simplify_specializations(std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif
