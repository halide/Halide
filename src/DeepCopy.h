#ifndef HALIDE_DEEP_COPY_H
#define HALIDE_DEEP_COPY_H

/** \file
 *
 * Defines pass to create deep-copies of all Functions in 'env'.
 */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Create deep-copies of all Functions in 'env'. This returns a pair of the
  * deep-copied versions of 'outputs' and 'env' */
std::pair<std::vector<Function>, std::map<std::string, Function>> deep_copy(
    const std::vector<Function> &outputs, const std::map<std::string, Function> &env);

}
}

#endif
