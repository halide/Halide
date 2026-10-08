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

// The tile shape of one mul_mat schedule: nt x mt outputs (N >= nt, M >= mt;
// edge tiles shift inwards) share each decoded weight row and activation
// column; `interleave` blocks per iteration into independent accumulators;
// parallel tasks of `rows` x `cols` outputs (rows 0: serial; cols 0: all of M).
struct Tiles {
    int nt = 1, mt = 1, interleave = 2, rows = 0, cols = 0;
};

// out's stage under `cond` (all of out if undefined) on ts tiles, each
// computing its region of dot; parallel tasks of ts.rows x ts.cols outputs.
// Returns dot's update under `cond`.
inline Stage tile_out(Func out, Func dot, Expr cond, Tiles ts) {
    Var n = out.args()[0], m = out.args()[1], no("no"), mo("mo"), ni("ni"), mi("mi"), nc("nc"), mc("mc");
    Stage s = cond.defined() ? out.specialize(cond) : Stage(out);
    s.tile(n, m, no, mo, ni, mi, ts.nt, ts.mt, TailStrategy::ShiftInwards).reorder(ni, mi, mo, no).unroll(mi);
    ts.nt > 1 ? s.vectorize(ni) : s.unroll(ni);
    dot.compute_at(out, mo);
    if (ts.cols) {
        s.split(mo, mc, mo, ts.cols / ts.mt, TailStrategy::GuardWithIf);
    }
    if (ts.rows) {
        s.split(no, nc, no, ts.rows / ts.nt, TailStrategy::GuardWithIf);
        if (ts.cols) s.reorder(ni, mi, mo, no, mc, nc).fuse(mc, nc, nc);
        s.parallel(nc);
    }
    return cond.defined() ? dot.update().specialize(cond) : dot.update();
}

// The weight quantized in blocks of `block`: per block and output a dot of
// the codes (when the activation is quantized alike, an int32 sdot: 4 products
// per lane; otherwise f32) times the hoisted scales, accumulated per lane in
// f32 (GGML's order), on ts tiles of out under `cond`.
inline void blocked(Func out, Func dot, Expr cond, Tiles ts, const RDom &r, int block, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t) {
    Stage s = tile_out(out, dot, cond, ts);
    Var n = out.args()[0], m = out.args()[1], mo("mo"), lane("lane"), u("u"), bacc("bacc");
    RVar ry("ry"), rx("rx"), rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    int per = integer ? 4 : 1, vec = t.natural_vector_size<int8_t>();
    s.split(r, ry, rx, block).split(rx, rxc, rxo, vec).split(rxo, rxo, rxi, per);
    Func blk = s.rfactor({{rxo, lane}, {ry, u}});
    blk.bound(lane, 0, vec / per);  // the lane's range, so the block's scales hoist
    blk.update().eager_inline(decoders(rs));
    Func codes = blk.update().hoist_invariants()[0];
    if (integer) codes = codes.change_type(Int(32));
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor({{rxo, lane}, {ryi, bacc}});
    Func lanes = s.rfactor(rxo, lane);
    s.atomic().vectorize(rxo).unroll(n).unroll(m);
    for (Func f : {lanes, acc, blk, codes}) {
        f.reorder_storage(lane, n, m);  // rfactor put the lanes last
    }
    for (Func f : {blk, codes}) {
        f.bound_storage(u, ts.interleave);  // also in the K tail: on the stack
    }
    lanes.compute_at(out, mo).vectorize(lane).unroll(n).unroll(m).update().vectorize(lane).unroll(ryi).unroll(n).unroll(m);
    acc.compute_at(out, mo).vectorize(lane).unroll(bacc).unroll(n).unroll(m);
    acc.update().reorder(lane, bacc, n, m, ryo).vectorize(lane).unroll(bacc).unroll(n).unroll(m);
    blk.compute_at(acc, ryo).vectorize(lane).unroll(u).unroll(n).unroll(m);
    blk.update().vectorize(lane).unroll(u).unroll(n).unroll(m);
    codes.compute_at(acc, ryo).vectorize(lane).unroll(u).unroll(n).unroll(m);
    codes.update().atomic().vectorize(rxi).vectorize(lane).unroll(rxc).unroll(u).unroll(n).unroll(m);
}

// The integer path on i8mm smmla (2 x 2 x 8 matrix-multiply tiles): nt x mt
// tiles of 2 x 2 sub-tiles, each 4 dense lanes of the intermediates' storage
// (split_storage), so the tile's accumulators stay in registers.
inline void blocked_mmla(Func out, Func dot, Expr cond, Tiles ts, const RDom &r, int block,
                         const std::vector<ApproximationResult> &rs) {
    Stage s = tile_out(out, dot, cond, ts);
    Var n = out.args()[0], m = out.args()[1], mo("mo"), nio("nio"), nii("nii"), mio("mio"), mii("mii"), u("u"), bacc("bacc");
    RVar ry("ry"), rx("rx"), rxc("rxc"), rxk("rxk"), ryo("ryo"), ryi("ryi");
    s.split(r, ry, rx, block).split(rx, rxc, rxk, 8);
    Func blk = s.rfactor(ry, u);
    blk.update().eager_inline(decoders(rs));
    Func codes = blk.update().hoist_invariants()[0].change_type(Int(32));
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor(ryi, bacc);
    // 2 x 2 sub-tiles, guarded: this code is also in other tiles' branches
    // (never run there), whose bounds must not grow past their tiles
    auto sub = [&](Stage st) {
        return st.split(n, nio, nii, 2, TailStrategy::GuardWithIf).split(m, mio, mii, 2, TailStrategy::GuardWithIf);
    };
    s.vectorize(n, 2, TailStrategy::GuardWithIf).unroll(n).unroll(m);
    for (Func f : {acc, blk, codes}) {
        Var x = f.args()[2], sno, sni, smo, smi;  // x: bacc or u
        f.split_storage(n, sno, sni, 2).split_storage(m, smo, smi, 2).reorder_storage(sni, smi, sno, smo, x);
        sub(f).reorder(nii, mii, nio, mio, x).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio).unroll(x);
    }
    for (Func f : {blk, codes}) {
        f.compute_at(acc, ryo).bound_storage(u, ts.interleave);
    }
    acc.compute_at(out, mo);
    sub(acc.update()).reorder(nii, mii, nio, mio, bacc, ryo).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio).unroll(bacc);
    sub(blk.update()).reorder(nii, mii, nio, mio, u).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio).unroll(u);
    sub(codes.update()).reorder(rxk, nii, mii, rxc, nio, mio, u).atomic().vectorize(rxk).vectorize(nii).vectorize(mii).unroll(rxc).unroll(nio).unroll(mio).unroll(u);
}

// The N = M = 1 schedule (GGML's vec_dot). Activations encoded inside the
// pipeline (`staged`) are encoded once, up front.
inline void vec_dot(Func out, Func dot, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t) {
    for (Func e : staged) {
        e.compute_root();
    }
    out.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);  // N = M = 1
    blocked(out, dot, Expr(), {}, r, blocks[0], blocks[1] == blocks[0], rs, t);
}

// An encoder computed per record (each block of activations), vectorized:
// its reductions (e.g. the block's scale) across the block, and the Funcs
// with an element per value (e.g. the codes) across their elements.
inline void encoder(Func e, const std::vector<ApproximationResult> &rs) {
    std::vector<Func> dec = decoders(rs);
    for (const ApproximationResult &res : rs) {
        for (Func f : res.encoded[0].name() == e.name() ? res.intermediates : std::vector<Func>{}) {
            if (f.name() == e.name() || std::any_of(dec.begin(), dec.end(), [&](const Func &d) { return d.name() == f.name(); })) continue;
            if (f.has_update_definition()) {
                f.compute_at(e, e.args()[0]).update().atomic().vectorize(f.rvars()[0]);
            } else if (f.dimensions() > e.dimensions()) {
                f.compute_at(e, e.args()[0]).vectorize(f.args()[0]);
            }
        }
    }
}

// mul_mat: gemm tiles the outputs (4 x 8 on smmla when the target has i8mm
// and both operands are quantized alike; else 4 x 4 on sdot); gemv (M = 1) is
// the vec_dot recipe on row pairs sharing each activation block (single rows
// for N = 1). Edge tiles shift inwards: any N, M at least the tile's.
inline void mul_mat(Func out, Func dot, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t) {
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    for (Func e : staged) {
        e.compute_root().specialize(M > 1).parallel(e.args().back());  // per activation row
        encoder(e, rs);
    }
    bool integer = blocks[1] == blocks[0];
    Tiles mmla{4, 8, 1, 32, 32}, gemm{4, 4, 1, 32, 32}, pairs{2, 1, 2, 32}, rows{1, 1, 2, 32};
    if (integer && t.has_feature(Target::ARMI8MM)) {
        blocked_mmla(out, dot, N >= mmla.nt && M >= mmla.mt, mmla, r, blocks[0], rs);
    }
    blocked(out, dot, N >= gemm.nt && M >= gemm.mt, gemm, r, blocks[0], integer, rs, t);
    blocked(out, dot, N >= pairs.nt, pairs, r, blocks[0], integer, rs, t);
    blocked(out, dot, Expr(), rows, r, blocks[0], integer, rs, t);
}

}  // namespace ggml
