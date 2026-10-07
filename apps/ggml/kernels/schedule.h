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

// The tile shape of one mul_mat schedule: nt x mt outputs (nt | N, mt | M)
// share each decoded weight row and activation column; `interleave` blocks per iteration into
// independent accumulators; parallel tasks of `rows` rows (0: serial).
struct Tiles {
    int nt = 1, mt = 1, interleave = 2, rows = 0;
};

// The weight quantized in blocks of `block`: per block and output a dot of
// the codes (when the activation is quantized alike, an int32 sdot: 4 products
// per lane; otherwise f32) times the hoisted scales, accumulated per lane in
// f32 (GGML's order). `s` is out's update (or a specialization of it).
inline void blocked(Func out, Stage s, Tiles ts, const RDom &r, int block, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t) {
    Var n = out.args()[0], m = out.args()[1], no("no"), mo("mo"), ni("ni"), mi("mi"), nc("nc"), lane("lane"), u("u"), bacc("bacc");
    RVar ry("ry"), rx("rx"), rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    int dot = integer ? 4 : 1, vec = t.natural_vector_size<int8_t>();
    s.split(r, ry, rx, block).split(rx, rxc, rxo, vec).split(rxo, rxo, rxi, dot);
    s.tile(n, m, no, mo, ni, mi, ts.nt, ts.mt, TailStrategy::RoundUp).reorder(ni, mi, mo, no);
    Func blk = s.rfactor({{rxo, lane}, {ry, u}});
    blk.bound(lane, 0, vec / dot);  // the lane's range, so the block's scales hoist
    blk.update().eager_inline(decoders(rs));
    Func codes = blk.update().hoist_invariants()[0];
    if (integer) codes = codes.change_type(Int(32));
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor({{rxo, lane}, {ryi, bacc}});
    Func lanes = s.rfactor(rxo, lane);
    s.atomic().vectorize(rxo).unroll(ni).unroll(mi);
    for (Func f : {lanes, acc, blk, codes}) {
        f.reorder_storage(lane, n, m);  // rfactor put the lanes last
    }
    lanes.compute_at(out, mo).vectorize(lane).unroll(n).unroll(m).update().vectorize(lane).unroll(ryi).unroll(ni).unroll(mi);
    acc.compute_at(out, mo).vectorize(lane).unroll(bacc).unroll(n).unroll(m);
    acc.update().reorder(lane, bacc, ni, mi, ryo).vectorize(lane).unroll(bacc).unroll(ni).unroll(mi);
    blk.compute_at(acc, ryo).vectorize(lane).unroll(u).unroll(n).unroll(m);
    blk.update().vectorize(lane).unroll(u).unroll(ni).unroll(mi);
    codes.compute_at(acc, ryo).vectorize(lane).unroll(u).unroll(n).unroll(m);
    codes.update().atomic().vectorize(rxi).vectorize(lane).unroll(rxc).unroll(u).unroll(ni).unroll(mi);
    if (ts.rows) {
        s.split(no, nc, no, ts.rows / ts.nt, TailStrategy::GuardWithIf).parallel(nc);
    }
}

// The N = M = 1 schedule (GGML's vec_dot). Activations encoded inside the
// pipeline (`staged`) are encoded once, up front.
inline void vec_dot(Func out, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t) {
    for (Func e : staged) {
        e.compute_root();
    }
    out.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);  // N = M = 1
    blocked(out, out.update(), {}, r, blocks[0], blocks[1] == blocks[0], rs, t);
}

// mul_mat: gemv (M = 1) is the vec_dot recipe per row; gemm tiles the outputs.
inline void mul_mat(Func out, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t) {
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    for (Func e : staged) {
        e.compute_root().specialize(M > 1).parallel(e.args().back());  // per activation row
    }
    bool integer = blocks[1] == blocks[0];
    Tiles gemm{4, 4, 1, 64}, rows{1, 1, 2, 32};
    blocked(out, out.update().specialize(N % gemm.nt == 0 && M % gemm.mt == 0), gemm, r, blocks[0], integer, rs, t);
    blocked(out, out.update(), rows, r, blocks[0], integer, rs, t);
}

}  // namespace ggml
