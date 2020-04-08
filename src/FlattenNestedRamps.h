#ifndef HALIDE_FLATTEN_NESTED_RAMPS_H
#define HALIDE_FLATTEN_NESTED_RAMPS_H

/** \file
 * Defines the lowering pass that flattens nested ramps and broadcasts.
 * */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement and replace nested ramps and broadcasts. */
Stmt flatten_nested_ramps(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
