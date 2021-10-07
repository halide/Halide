#ifndef HALIDE_RUNTIME_CONSTANTS_H
#define HALIDE_RUNTIME_CONSTANTS_H

/** \file
 *
 * This file contains private constants shared between the Halide
 * library and the Halide runtime. These constants are not part of the
 * public API of the runtime.
 */

/** The threshold at which "stack" allocations should actually be backed by the heap. */
namespace Halide {
namespace Runtime {
namespace Internal {
namespace Constants {
static constexpr size_t maximum_stack_allocation_bytes = 16384;
}
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif
