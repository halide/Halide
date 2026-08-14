#ifndef HALIDE_RUNTIME_CONSTANTS_H
#define HALIDE_RUNTIME_CONSTANTS_H

/** \file
 *
 * This file contains private constants shared between the Halide
 * library and the Halide runtime. These constants are not part of the
 * public API of the runtime.
 */

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Constants {

/** The threshold at which "stack" allocations should actually be backed by the heap. */
static constexpr int maximum_stack_allocation_bytes = 16384;

/** Marks a read of one entry of a tensor core accumulator in generated PTX.
 * How the hardware spreads a fragment across the registers of a warp isn't
 * architecturally specified, so the CUDA runtime measures it and rewrites
 * these markers into real shuffles before handing the module to the driver. A
 * marker looks like
 *
 *   // halide_wmma_get row=3 col=9 regs=%r7,%r8,%r9,%r10,%r11,%r12,%r13,%r14
 *   shfl.sync.idx.b32 %r20, %r7, 0, 31, -1;
 *
 * where the shuffle is a placeholder that keeps the unpatched module
 * assemblable. The runtime overwrites both lines with a shuffle of the
 * register that holds the entry from the lane that holds it, padded with
 * spaces so that the module stays the same size. */
static constexpr const char *wmma_get_element_marker = "halide_wmma_get";

}  // namespace Constants
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif
