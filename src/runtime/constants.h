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

/** How the hardware spreads a fragment across the registers of a warp isn't
 * architecturally specified, so anything that depends on it is left for the
 * CUDA runtime to finish off once it has measured the layout, just before it
 * hands the module to the driver.
 *
 * Most of what depends on it is one instruction whose operands aren't known
 * yet, so it is emitted as a pretend instruction: a number saying what is
 * wanted, then the result, then every register the fragment lives in.
 *
 *   halide_wmma_get 57, %r20, %r7, %r8, %r9, %r10, %r11, %r12, %r13, %r14;
 *
 * The runtime writes the real instruction over the whole statement and pads
 * what is left with spaces:
 *
 *   shfl.sync.idx.b32 %r20, %r9, 14, 31, -1;
 *
 * which always fits, because the real instruction names two of the registers
 * where the marker names them all. Since what is wanted comes first, the
 * runtime knows which registers to keep by the time they go past, so it can
 * rewrite a marker in one pass over it, copying the operands it keeps and
 * writing over the ones it doesn't. It never needs to know a register name.
 * None of these are real opcodes, so a module that reached the driver
 * unfinished is rejected rather than quietly meaning something else.
 *
 * A get reads one entry of an accumulator, given as a row-major index into the
 * matrix, and becomes a shuffle of the register holding it from the lane
 * holding it. */
static constexpr const char *wmma_get_element_marker = "halide_wmma_get";

/** Builds one register of an A operand out of an accumulator holding the same
 * matrix, which a fused chain of matrix multiplies needs. Measured on sm_120
 * the two layouts line up lane for lane, so this is a convert and a pack with
 * no cross-lane traffic, and all the runtime decides is which two registers of
 * the accumulator feed the register asked for. It becomes
 *
 *   cvt.rn.f16x2.f32 %r30, %r9, %r12;
 *
 * which takes the high half first. */
static constexpr const char *wmma_pack_element_marker = "halide_wmma_pack";

/** Marks the building of one register of an accumulator out of a vector that
 * every lane holds a whole copy of, indexed by the row or the column of the
 * matrix. This is what a value like `prod(x, y) - m(y)` needs: the subtrahend
 * covers the tile, but as a vector broadcast along one axis rather than as a
 * fragment, so which entry of it a lane wants varies from lane to lane.
 *
 * That rules out becoming a single instruction, since one instruction stream
 * serves all 32 lanes. So unlike the others this one is a whole select tree
 * over the vector, driven by four predicates, and all the runtime fills in is
 * which bit of the lane index drives each one. It looks like
 *
 *   { .reg .u32 %hbl; .reg .u32 %hbm<4>; .reg .pred %hbp<4>; .reg .f32 %hb<31>;
 *   mov.u32 %hbl, %laneid;
 *   // halide_wmma_build 3
 *   and.b32 %hbm0, %hbl, ??;
 *   setp.ne.u32 %hbp0, %hbm0, ?;
 *   ... one pair per bit of the index ...
 *   mov.f32 %hb0, $1;
 *   ... one per entry of the vector ...
 *   selp.f32 %hb16, %hb1, %hb0, %hbp0;
 *   ... the tree ...
 *   mov.f32 $0, %hb30; }
 *
 * The number packs which axis of the matrix indexes the vector (zero for the
 * row) and which register of the accumulator is being built. The fields are
 * the last thing on their statement, so the runtime finds them by counting
 * semicolons rather than parsing: the mask picks out the lane bit that drives
 * that predicate, and a mask of zero with a comparand of one makes the
 * predicate constant, which is how an index bit that doesn't vary with the
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

/** Exchanges the entries of an accumulator with the ones some distance away
 * along one axis of the matrix, which is the step a reduction along that axis
 * is built out of. Entry (row, col) takes the value of the entry whose index
 * along the axis is its own with one bit flipped, so repeating it once per bit
 * leaves every entry holding the whole row or column's worth combined.
 *
 * Where the two entries live decides what the step costs, and only the runtime
 * knows: if that bit of the index picks the register then the exchange happens
 * within a lane, and if it picks the lane then it is a butterfly shuffle. Both
 * come out as
 *
 *   shfl.sync.bfly.b32 %r20, %r9, 2, 31, -1;
 *
 * because a butterfly by zero returns the lane its own value. The number the
 * marker carries packs which register of the result is being built, which bit
 * of the index is flipped, and which axis it indexes - zero for the row. */
static constexpr const char *wmma_xor_element_marker = "halide_wmma_xor";

}  // namespace Constants
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif
