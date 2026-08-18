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

/** What a field the runtime has to fill in looks like before it does. Every
 * field is written over in place, so it starts out as many of these as the
 * value that replaces it will be wide. It is not a digit, so a module that
 * reached the driver unpatched is rejected rather than quietly meaning
 * something else. */
static constexpr char wmma_field_placeholder = '?';

/** Marks a read of one entry of a tensor core accumulator in generated PTX.
 * How the hardware spreads a fragment across the registers of a warp isn't
 * architecturally specified, so the CUDA runtime measures it and finishes
 * these off before handing the module to the driver. A marker looks like
 *
 *   // halide_wmma_get 39
 *   { .reg .f32 %hget<8>;
 *   mov.f32 %hget0, %r7;
 *   ... one per register the accumulator lives in ...
 *   shfl.sync.idx.b32 %r20, %hget?, ??, 31, -1; }
 *
 * The number is the entry wanted, as a row-major index into the matrix, in
 * hex - so for a 16x16 matrix its two digits read as the row and the column,
 * and every value it can take is a real entry. The
 * fragment is copied into a nested scope so that the shuffle can name its
 * source by an index the compiler chose rather than one only the register
 * allocator knows, which leaves the runtime two fields to fill in: the
 * register holding the entry, and the lane holding it. Both are written over
 * what is already there, so nothing changes length, and ptxas folds away the
 * copies it didn't use. */
static constexpr const char *wmma_get_element_marker = "halide_wmma_get";

/** How the fields of a marker are written, so that patching one in doesn't
 * change the length of anything. The entry is zero padded hex, which the
 * runtime reads. The lane is decimal, because PTX reads it, and right aligned
 * and padded with spaces rather than zeroes, because a leading zero would make
 * PTX take it as octal. The source register is a single digit, so a fragment
 * can't live in more than ten registers. */
static constexpr int wmma_get_entry_digits = 2;
static constexpr int wmma_get_lane_digits = 2;

/** Marks the building of an A operand out of an accumulator holding the same
 * matrix, which a fused chain of matrix multiplies needs. Measured on sm_120
 * the two layouts line up lane for lane, so this is a convert and a pack with
 * no cross-lane traffic, and all the runtime fills in is which two registers
 * of the accumulator feed each register of the operand. A marker looks like
 *
 *   // halide_wmma_pack 3
 *   { .reg .f32 %hget<8>;
 *   mov.f32 %hget0, %r7;
 *   ... one per register the accumulator lives in ...
 *   cvt.rn.f16x2.f32 %r30, %hget?, %hget?; }
 *
 * The number is which register of the operand is being built, in hex. The
 * convert takes the high half first, so the runtime overwrites the first index
 * with the register feeding the high half and the second with the one feeding
 * the low half. */
static constexpr const char *wmma_pack_element_marker = "halide_wmma_pack";

/** What follows the source lane in a marker's shuffle. The runtime checks for
 * this to make sure it has found the operand it means to overwrite. */
static constexpr const char *wmma_get_shuffle_tail = ", 31, -1;";

/** Marks the building of one register of an accumulator out of a vector that
 * every lane holds a whole copy of, indexed by the row or the column of the
 * matrix. This is what a value like `prod(x, y) - m(y)` needs: the subtrahend
 * covers the tile, but as a vector broadcast along one axis rather than as a
 * fragment, so which entry of it a lane wants varies from lane to lane.
 *
 * That rules out patching to a single instruction, since one instruction
 * stream serves all 32 lanes. Instead the marker is a select tree over the
 * whole vector, driven by four predicates, and all the runtime fills in is
 * which bit of the lane index drives each one. A marker looks like
 *
 *   { .reg .u32 %hbl; .reg .u32 %hbm<4>; .reg .pred %hbp<4>; .reg .f32 %hb<31>;
 *   mov.u32 %hbl, %laneid;
 *   // halide_wmma_build 03
 *   and.b32 %hbm0, %hbl, ??;
 *   setp.ne.u32 %hbp0, %hbm0, ?;
 *   ... one pair per bit of the index ...
 *   mov.f32 %hb0, $1;
 *   ... one per entry of the vector ...
 *   selp.f32 %hb16, %hb1, %hb0, %hbp0;
 *   ... the tree ...
 *   mov.f32 $0, %hb30; }
 *
 * The number is in hex, and reads as which axis of the matrix indexes the
 * vector (zero for the row) and which register of the accumulator is being
 * built.
 * The fields are the last thing on their statement, so the runtime finds them by
 * counting semicolons rather than parsing: the mask picks out the lane bit
 * that drives that predicate, and a mask of zero with a comparand of one makes
 * the predicate constant, which is how an index bit that doesn't vary with the
 * lane is said. ptxas folds away the halves of the tree that a constant
 * predicate makes unreachable. */
static constexpr const char *wmma_build_element_marker = "halide_wmma_build";

/** The width of a vector a fragment can be built out of, as a number of bits
 * of index. Four, because the tile is 16x16. */
static constexpr int wmma_build_index_bits = 4;

/** How the fields of a build marker are written. The mask is decimal and right
 * aligned with a space rather than a zero, because a leading zero would make
 * PTX read it as octal. The comparand is a single digit. */
static constexpr int wmma_build_mask_digits = 2;
static constexpr int wmma_build_compare_digits = 1;

/** Marks an exchange of the entries of an accumulator with the entries some
 * distance away along one axis of the matrix, which is the step a reduction
 * along that axis is built out of. Entry (row, col) takes the value of the
 * entry whose index along the axis is its own with one bit flipped, so
 * repeating it once per bit leaves every entry holding the whole row or
 * column's worth combined.
 *
 * Where the two entries live decides what the step costs, and only the runtime
 * knows: if that bit of the index picks the register then the exchange happens
 * within a lane, and if it picks the lane then it is a butterfly shuffle. Both
 * are the same instruction with the mask filled in differently, because a
 * butterfly by zero returns the lane its own value. A marker looks like
 *
 *   // halide_wmma_xor 13
 *   { .reg .f32 %hget<8>;
 *   mov.f32 %hget0, %r7;
 *   ... one per register the accumulator lives in ...
 *   shfl.sync.bfly.b32 %r20, %hget?, ??, 31, -1; }
 *
 * The number is in hex, and reads as which register of the result is being
 * built, which bit of the index is flipped, and which axis it indexes - zero
 * for the row. The fields filled in are the same two the get marker has: which
 * register the value comes from, and which lane, except that here the lane is
 * relative rather than absolute. */
static constexpr const char *wmma_xor_element_marker = "halide_wmma_xor";

}  // namespace Constants
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif
