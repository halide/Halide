#pragma once

// Schedules of the mul_mat algorithm (kernels/matmul.cpp). They see its
// structure only: the reduction r over k, the values per record of each
// operand, and the operands' ApproximationResults.
#include "Halide.h"

namespace ggml {

using namespace Halide;

// The decode Funcs eager_inline can fold into a consumer.
inline std::vector<Func> decoders(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> fs;
    for (const ApproximationResult &res : rs) {
        std::vector<Func> d = res.decode_funcs();
        fs.insert(fs.end(), d.begin(), d.end());
    }
    return fs;
}

// The weight quantized in blocks of `block`: per block a dot of the codes
// (when the activation is quantized alike, an int32 sdot: 4 products per
// lane; otherwise f32) times the hoisted scales, accumulated per lane in f32
// (GGML's order), `interleave` blocks per iteration into independent
// accumulators.
inline void vec_dot_blocked(Func out, const RDom &r, int block, bool integer,
                            const std::vector<ApproximationResult> &rs, const Target &t, int interleave = 2) {
    Var n = out.args()[0], lane("lane"), u("u"), bacc("bacc");
    RVar ry("ry"), rx("rx"), rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    int dot = integer ? 4 : 1;
    out.update().split(r, ry, rx, block).split(rx, rxc, rxo, t.natural_vector_size<int8_t>()).split(rxo, rxo, rxi, dot);
    Func blk = out.update().rfactor({{rxo, lane}, {ry, u}});
    blk.update().eager_inline(decoders(rs));
    Func codes = blk.update().hoist_invariants()[0];
    if (integer) codes = codes.change_type(Int(32));
    out.update().split(ry, ryo, ryi, interleave, TailStrategy::GuardWithIf);
    Func acc = out.update().rfactor({{rxo, lane}, {ryi, bacc}});
    Func lanes = out.update().rfactor(rxo, lane);
    out.update().atomic().vectorize(rxo);
    lanes.compute_at(out, n).vectorize(lane).update().vectorize(lane).unroll(ryi);
    acc.compute_at(out, n).vectorize(lane).unroll(bacc).update().vectorize(lane).unroll(bacc);
    blk.compute_at(acc, ryo).vectorize(lane).unroll(u).update().vectorize(lane).unroll(u);
    codes.compute_at(acc, ryo).vectorize(lane).unroll(u);
    codes.update().atomic().vectorize(rxi).vectorize(lane).unroll(rxc).unroll(u);
}

// The N = M = 1 schedule (GGML's vec_dot).
inline void vec_dot(Func out, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const Target &t) {
    out.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);  // N = M = 1
    vec_dot_blocked(out, r, blocks[0], blocks[1] == blocks[0], rs, t);
}

}  // namespace ggml
