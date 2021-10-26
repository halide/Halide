#ifndef HALIDE_RUNTIME_CONSTANTS_H
#define HALIDE_RUNTIME_CONSTANTS_H

/** \file
 *
 * This file contains private constants shared between the Halide
 * library and the Halide runtime. These constants are not part of the
 * public API of the runtime.
 */

#include "HalideRuntime.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Constants {

/** The threshold at which "stack" allocations should actually be backed by the heap. */
static constexpr size_t maximum_stack_allocation_bytes = 16384;

}  // namespace Constants
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif
