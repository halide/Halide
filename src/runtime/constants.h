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

/** The number of registers each lane of a warp holds a 16x16 single precision
 * tensor core accumulator in. */
static constexpr int wmma_accumulator_registers = 8;

/** Marks a read of one entry of a tensor core accumulator in generated PTX.
 * How the hardware spreads a fragment across the registers of a warp isn't
 * architecturally specified, so the CUDA runtime measures it and finishes
 * these off before handing the module to the driver. A marker looks like
 *
 *   // halide_wmma_get 057
 *   shfl.sync.idx.b32 %r20, %r7,  0, 31, -1;
 *   shfl.sync.idx.b32 %r20, %r8,  0, 31, -1;
 *   ... one per register the accumulator lives in ...
 *
 * The number is the entry wanted, as a row-major index into the matrix,
 * written to a fixed width. Every register the fragment could be holding it in
 * gets its own shuffle, so that the runtime picks one rather than composing an
 * instruction out of register names only the compiler knows. It blanks out the
 * ones it doesn't want and writes the lane that holds the entry over the one
 * it keeps, so nothing changes length. */
static constexpr const char *wmma_get_element_marker = "halide_wmma_get";

/** The entry index in a marker is written to this many digits, zero padded,
 * and the lane in each of its shuffles to this many, right aligned and padded
 * with spaces, so that patching one in doesn't change the length of anything.
 * A lane can't be zero padded because PTX reads a leading zero as octal. */
static constexpr int wmma_get_entry_digits = 3;
static constexpr int wmma_get_lane_digits = 2;

/** What follows the source lane in a marker's shuffle. The runtime finds the
 * lane to overwrite by measuring back from the end of the instruction. */
static constexpr const char *wmma_get_shuffle_tail = ", 31, -1;";

}  // namespace Constants
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif
