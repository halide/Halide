#pragma once

// Shared "make an Approximation-decoded block reduction accumulate as an
// integer dot product" schedule, used by both the vec_dot and repack matmul
// Generators. Given a reduction `acc` of the shape
//   acc(...) += decode(Wt)(r.x, r.y, ...) * decode(Vec)(r.x, r.y)
// whose operands' per-block scales are invariant across the within-block
// reduction r.x (but vary with the block index r.y and any output dims), this
// derives the scale-free Int(32) inner dot the caller then schedules.
//
// Why the deep inline: hoist_invariants() needs each operand's per-block scale
// to appear as a top-level factor of the (rfactored) update product. A single
// eager_inline() of an ApproximationResult's .replacement only peels the
// outermost relayout wrapper; the scale*codes product lives deeper, inside the
// dequantizer Func the Approximation combinators build. eager_inline() no-ops
// on any Func not currently directly called and flattens exposed calls left to
// right, so inlining the whole set of inlinable decode handles -- one pass per
// possible chain level -- flattens the decode chains of every operand
// regardless of build order, leaving (codes*scale)*... with the scales as
// loop-invariant leaves. See doc: the SDOT investigation on ggml-on-qk.

#include "Halide.h"

#include <vector>

namespace ggml_halide {

// rfactor `acc` preserving `preserved` (typically {{r.y, u}} -- the block
// index), flatten every operand's decode chain into the resulting per-block
// partial, hoist the now-invariant scales out of the remaining reduction, and
// retype the scale-free inner dot to Int(32). Returns that Int(32) Func -- it
// holds the real reduction, so the caller schedules *it* (compute_root,
// vectorize the within-block RVar, etc.).
// `distribute` multiplies the per-block product out before hoisting, for
// formats whose decode carries an offset: an affine weight makes the product
// (d*code + m) * (d_act*act), which has no single scale to hoist. Multiplied out
// it is d*d_act * sum(code*act) + m*d_act * sum(act), and hoist_invariants()
// gives each term its own accumulator -- both with integer bodies, so both reach
// SDOT. That is ggml's own decomposition of the affine formats.
inline std::vector<Halide::Func> sdot_partial(Halide::Func &acc,
                                              const std::vector<std::pair<Halide::RVar, Halide::Var>> &preserved,
                                              const std::vector<Halide::ApproximationResult> &operands,
                                              bool distribute = false) {
    using namespace Halide;

    Func acc_dot = acc.update().rfactor(preserved);

    std::vector<Func> decode_funcs;
    for (const ApproximationResult &op : operands) {
        decode_funcs.push_back(op.replacement);
    }
    for (const ApproximationResult &op : operands) {
        for (const Func &h : op.handles) {
            if (h.function().can_be_inlined()) {
                decode_funcs.push_back(h);
            }
        }
    }
    for (size_t pass = 0; pass < decode_funcs.size(); pass++) {
        acc_dot.update().eager_inline(decode_funcs);
    }

    if (distribute) {
        acc_dot.update().distribute();
    }

    // One accumulator per term -- one for a symmetric weight, two once an affine
    // weight's product has been multiplied out. Each is its own Func, so each
    // retypes on its own.
    std::vector<Func> parts;
    for (Func &part : acc_dot.update().hoist_invariants()) {
        parts.push_back(part.change_type(Int(32)));
    }
    return parts;
}

}  // namespace ggml_halide
