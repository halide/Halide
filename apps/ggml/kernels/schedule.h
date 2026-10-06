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

// Both operands quantized in blocks of `block`: per block an int32 dot of
// the codes (sdot: 4 products per lane) times the product of the scales,
// accumulated per lane in f32 (GGML's order), `interleave` blocks per
// iteration into independent accumulators.
inline void vec_dot_integer(Func out, const RDom &r, int block, const std::vector<ApproximationResult> &rs,
                            const Target &t, int interleave = 2) {
    Var n = out.args()[0], lane("lane"), u("u"), bacc("bacc");
    RVar ry("ry"), rx("rx"), rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    out.update().split(r, ry, rx, block).split(rx, rxc, rxo, t.natural_vector_size<int8_t>()).split(rxo, rxo, rxi, 4);
    Func blk = out.update().rfactor({{rxo, lane}, {ry, u}});
    blk.update().eager_inline(decoders(rs));
    Func codes = blk.update().hoist_invariants()[0].change_type(Int(32));
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

// Otherwise: decode the weight to f32 lanes, a block per iteration, and FMA.
inline void vec_dot_float(Func out, const RDom &r, int block, const Target &t) {
    Var n = out.args()[0], lane("lane"), v("v");
    RVar ry("ry"), rx("rx"), rxo("rxo"), rxi("rxi");
    out.update().split(r, ry, rx, block).split(rx, rxo, rxi, t.natural_vector_size<float>());
    Func acc = out.update().rfactor({{rxo, lane}, {rxi, v}});
    out.update().atomic().vectorize(rxi).unroll(rxo);
    acc.compute_at(out, n).vectorize(v).unroll(lane).update().vectorize(v).unroll(lane);
}

// The N = M = 1 schedule (GGML's vec_dot).
inline void vec_dot(Func out, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const Target &t) {
    out.bound(out.args()[0], 0, 1).bound(out.args()[1], 0, 1);
    if (blocks[0] > 1 && blocks[1] == blocks[0]) {
        vec_dot_integer(out, r, blocks[0], rs, t);
    } else {
        vec_dot_float(out, r, blocks[0], t);
    }
}

}  // namespace ggml
