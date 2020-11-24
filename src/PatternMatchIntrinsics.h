#ifndef HALIDE_PATTERN_MATCH_INTRINSICS_H
#define HALIDE_PATTERN_MATCH_INTRINSICS_H

/** \file
 * Tools for optimizing IR for Hexagon.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Replace common arithmetic patterns with intrinsics. */
Stmt pattern_match_intrinsics(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
