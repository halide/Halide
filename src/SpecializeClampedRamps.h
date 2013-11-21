#ifndef HALIDE_SPECIALIZE_CLAMPED_RAMPS_H
#define HALIDE_SPECIALIZE_CLAMPED_RAMPS_H

/** \file
 * Defines a lowering pass that simplifies code using clamped ramps.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement with multi-dimensional Realize, Provide, and Call
 * nodes, and turn it into a statement with single-dimensional
 * Allocate, Store, and Load nodes respectively. */
Stmt specialize_clamped_ramps(Stmt s);

}
}

#endif
