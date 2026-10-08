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
// share each decoded weight row and activation column; `interleave` blocks
// per iteration into independent accumulators; parallel tasks of `rows` x
// `cols` outputs (rows 0: serial; cols 0: all of M).
struct Tiles {
    int nt = 1, mt = 1, interleave = 2, rows = 0, cols = 0;
};

// Parallel tasks of ts.rows x ts.cols outputs of the tiled update `s`.
inline void tasks(Stage s, Tiles ts, Var ni, Var mi, Var no, Var mo) {
    Var nc("nc"), mc("mc");
    if (ts.cols) {
        s.split(mo, mc, mo, ts.cols / ts.mt, TailStrategy::GuardWithIf);
    }
    if (ts.rows) {
        s.split(no, nc, no, ts.rows / ts.nt, TailStrategy::GuardWithIf);
        if (ts.cols) s.reorder(ni, mi, mo, no, mc, nc).fuse(mc, nc, nc);
        s.parallel(nc);
    }
}

// The weight quantized in blocks of `block`: per block and output a dot of
// the codes (when the activation is quantized alike, an int32 sdot: 4 products
// per lane; otherwise f32) times the hoisted scales, accumulated per lane in
// f32 (GGML's order). `s` is out's update (or a specialization of it).
inline void blocked(Func out, Stage s, Tiles ts, const RDom &r, int block, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t) {
    Var n = out.args()[0], m = out.args()[1], no("no"), mo("mo"), ni("ni"), mi("mi"), lane("lane"), u("u"), bacc("bacc");
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
    for (Func f : {blk, codes}) {
        f.bound_storage(u, ts.interleave);  // also in the K tail: on the stack
    }
    lanes.compute_at(out, mo).vectorize(lane).unroll(n).unroll(m).update().vectorize(lane).unroll(ryi).unroll(ni).unroll(mi);
    acc.compute_at(out, mo).vectorize(lane).unroll(bacc).unroll(n).unroll(m);
    acc.update().reorder(lane, bacc, ni, mi, ryo).vectorize(lane).unroll(bacc).unroll(ni).unroll(mi);
    blk.compute_at(acc, ryo).vectorize(lane).unroll(u).unroll(n).unroll(m);
    blk.update().vectorize(lane).unroll(u).unroll(ni).unroll(mi);
    codes.compute_at(acc, ryo).vectorize(lane).unroll(u).unroll(n).unroll(m);
    codes.update().atomic().vectorize(rxi).vectorize(lane).unroll(rxc).unroll(u).unroll(ni).unroll(mi);
    tasks(s, ts, ni, mi, no, mo);
}

// The integer path on i8mm smmla (2 x 2 x 8 matrix-multiply tiles): nt x mt
// tiles of 2 x 2 sub-tiles, each 4 dense lanes of the intermediates' storage
// (split_storage), so the tile's accumulators stay in registers.
inline void blocked_mmla(Func out, Stage s, Tiles ts, const RDom &r, int block,
                         const std::vector<ApproximationResult> &rs) {
    Var n = out.args()[0], m = out.args()[1], no("no"), mo("mo"), ni("ni"), mi("mi"), nio("nio"), nii("nii"), mio("mio"), mii("mii"), u("u"), bacc("bacc");
    RVar ry("ry"), rx("rx"), rxc("rxc"), rxk("rxk"), ryo("ryo"), ryi("ryi");
    s.split(r, ry, rx, block).split(rx, rxc, rxk, 8);
    s.tile(n, m, no, mo, ni, mi, ts.nt, ts.mt, TailStrategy::RoundUp).reorder(ni, mi, mo, no);
    Func blk = s.rfactor(ry, u);
    blk.update().eager_inline(decoders(rs));
    Func codes = blk.update().hoist_invariants()[0].change_type(Int(32));
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor(ryi, bacc);
    s.vectorize(ni, 2).unroll(ni).unroll(mi);
    for (Func f : {acc, blk, codes}) {
        Var x = f.args()[2], sno, sni, smo, smi;  // x: bacc or u
        f.split_storage(n, sno, sni, 2).split_storage(m, smo, smi, 2).reorder_storage(sni, smi, sno, smo, x);
        f.split(n, nio, nii, 2).split(m, mio, mii, 2).reorder(nii, mii, nio, mio, x).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio).unroll(x);
    }
    for (Func f : {blk, codes}) {
        f.compute_at(acc, ryo).bound_storage(u, ts.interleave);
    }
    acc.compute_at(out, mo);
    acc.update().split(ni, nio, nii, 2).split(mi, mio, mii, 2).reorder(nii, mii, nio, mio, bacc, ryo).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio).unroll(bacc);
    blk.update().split(ni, nio, nii, 2).split(mi, mio, mii, 2).reorder(nii, mii, nio, mio, u).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio).unroll(u);
    codes.update().split(ni, nio, nii, 2).split(mi, mio, mii, 2).reorder(rxk, nii, mii, rxc, nio, mio, u).atomic().vectorize(rxk).vectorize(nii).vectorize(mii).unroll(rxc).unroll(nio).unroll(mio).unroll(u);
    tasks(s, ts, ni, mi, no, mo);
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

// mul_mat: gemm tiles the outputs (4 x 8 on smmla when the target has i8mm
// and both operands are quantized alike; else 4 x 4 on sdot); gemv (M = 1) is
// the vec_dot recipe on row pairs sharing each activation block (single rows
// for odd N).
inline void mul_mat(Func out, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t) {
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    for (Func e : staged) {
        e.compute_root().specialize(M > 1).parallel(e.args().back());  // per activation row
    }
    bool integer = blocks[1] == blocks[0];
    Tiles mmla{4, 8, 1, 32, 32}, gemm{4, 4, 1, 32, 32}, pairs{2, 1, 2, 32}, rows{1, 1, 2, 32};
    if (integer && t.has_feature(Target::ARMI8MM)) {
        blocked_mmla(out, out.update().specialize(N % mmla.nt == 0 && M % mmla.mt == 0), mmla, r, blocks[0], rs);
    }
    blocked(out, out.update().specialize(N % gemm.nt == 0 && M % gemm.mt == 0), gemm, r, blocks[0], integer, rs, t);
    blocked(out, out.update().specialize(N % pairs.nt == 0), pairs, r, blocks[0], integer, rs, t);
    blocked(out, out.update(), rows, r, blocks[0], integer, rs, t);
}

}  // namespace ggml
