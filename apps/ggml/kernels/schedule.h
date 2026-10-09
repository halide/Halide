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

// The runtime's thread count.
inline Expr threads() {
    return max(Internal::Call::make(Int(32), "halide_get_num_threads", {}, Internal::Call::PureExtern), 1);
}

// The tile shape of one mul_mat schedule: nt x mt outputs (N >= nt, M >= mt;
// edge tiles shift inwards; in out's updates, M tails are guarded) share each decoded weight row and activation
// column; `interleave` blocks per iteration into independent accumulators;
// parallel tasks of `rows` x `cols` outputs (rows 0: serial; cols 0: all of M),
// fewer rows if that leaves under `tasks` tasks per thread.
struct Tiles {
    int nt = 1, mt = 1, interleave = 2, rows = 0, cols = 0, tasks = 0;
};

// Out's stage `st`: 0 is its pure definition, over all of M; i > 0 its i-th
// update, over the rows of one RDom. Its row loops are named per stage.
inline VarOrRVar row_var(Func out, int st, const std::string &name = "") {
    if (!st) return name.empty() ? out.args()[1] : Var(name);
    return name.empty() ? out.rvars(st - 1)[0] : RVar(name + std::to_string(st));
}

// The loop of out's stage st that computes a tile.
inline LoopLevel tile_at(Func out, int st) {
    return LoopLevel(out, row_var(out, st, "mo"));
}

// out's stage st under `cond` (all of it if undefined) on ts tiles, each
// computing its region of dot; parallel tasks of ts.rows x ts.cols outputs.
// `whole_n`: N is a multiple of ts.nt. Returns dot's update under `cond`.
inline Stage tile_out(Func out, int st, Func dot, Expr cond, Tiles ts, bool whole_n = false) {
    VarOrRVar n = out.args()[0], m = row_var(out, st), mo = row_var(out, st, "mo"), mi = row_var(out, st, "mi"), mc = row_var(out, st, "mc"), t = row_var(out, st, "nc");
    Var no("no"), ni("ni"), nc("nc");
    Stage o = st ? out.update(st - 1) : Stage(out), s = cond.defined() ? o.specialize(cond) : o;
    // Updates: whole tiles in n (cond knows N % nt == 0); m an RVar.
    s.split(n, no, ni, ts.nt, st || whole_n ? TailStrategy::RoundUp : TailStrategy::ShiftInwards);
    s.split(m, mo, mi, ts.mt, st ? TailStrategy::GuardWithIf : TailStrategy::ShiftInwards);
    s.reorder(ni, mi, mo, no).unroll(mi);
    ts.nt > 1 ? s.vectorize(ni) : s.unroll(ni);
    dot.compute_at(tile_at(out, st));
    if (ts.cols) {
        s.split(mo, mc, mo, ts.cols / ts.mt, TailStrategy::GuardWithIf);
    }
    if (ts.rows) {
        Expr per = ts.rows / ts.nt;
        if (ts.tasks) {
            Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
            Expr cs = ts.cols ? (M + ts.cols - 1) / ts.cols : 1, ns = (ts.tasks * threads() + cs - 1) / cs;
            per = clamp(((N + ts.nt - 1) / ts.nt + ns - 1) / ns, 1, per);
        }
        s.split(no, nc, no, per, TailStrategy::GuardWithIf);
        if (ts.cols) s.reorder(ni, mi, mo, no, mc, nc).fuse(mc, nc, t);
        s.parallel(ts.cols ? t : VarOrRVar(nc));
    }
    return cond.defined() ? dot.update().specialize(cond) : dot.update();
}

// The weight quantized in blocks of `block`: per block and output a dot of
// the codes (when the activation is quantized alike, an int32 sdot: 4 products
// per lane; otherwise f32) times the hoisted scales, accumulated per lane in
// f32 (GGML's order), on ts tiles of out under `cond`.
inline void blocked(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int block, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t) {
    Stage s = tile_out(out, st, dot, cond, ts);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), u("u"), bacc("bacc");
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
    lanes.compute_at(tile_at(out, st)).vectorize(lane).unroll(n).unroll(m).update().vectorize(lane).unroll(ryi).unroll(n).unroll(m);
    acc.compute_at(tile_at(out, st)).vectorize(lane).unroll(bacc).unroll(n).unroll(m);
    acc.update().reorder(lane, bacc, n, m, ryo).vectorize(lane).unroll(bacc).unroll(n).unroll(m);
    blk.compute_at(acc, ryo).vectorize(lane).unroll(u).unroll(n).unroll(m);
    blk.update().vectorize(lane).unroll(u).unroll(n).unroll(m);
    codes.compute_at(acc, ryo).vectorize(lane).unroll(u).unroll(n).unroll(m);
    codes.update().atomic().vectorize(rxi).vectorize(lane).unroll(rxc).unroll(u).unroll(n).unroll(m);
}

// The integer path for weight records of ts.nt rows interleaved in pieces of
// `chunk` codes (as GGML's repacked gemv): per block, int32 sums vectorized
// across the record's rows and each piece's 4-code quads (sdot), so a piece
// of the record is one dense load and the act's piece is broadcast to its
// rows (held in registers per block: with 4-code pieces, a by-element sdot);
// the quads are then merged, and the scales and accumulators are vectorized
// across the rows.
inline void blocked_rows(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int block, int chunk,
                         const std::vector<ApproximationResult> &rs) {
    Stage s = tile_out(out, st, dot, cond, ts, true);
    Var n = dot.args()[0], m = dot.args()[1], q("q"), u("u"), bacc("bacc");
    // ryi_rows: not ryi, as dot's specializations share bounds by RVar name (gap M)
    RVar ry("ry"), rx("rx"), rp("rp"), rq("rq"), ri("ri"), ryo("ryo"), ryi("ryi_rows");
    s.split(r, ry, rx, block);
    Func blk = s.rfactor(ry, u);
    // The act's codes (integer decode Funcs of rs[1..]; rs[0] is the weight's)
    // stay out of the inline: staged per block in registers, below.
    std::vector<Func> dec, ac;
    for (Func f : decoders(rs)) {
        bool act = false;
        for (size_t i = 1; i < rs.size(); i++) {
            for (const Func &g : rs[i].decode_trace.intermediates) {
                act |= g.name() == f.name() && f.type().is_int();
            }
        }
        (act ? ac : dec).push_back(f);
    }
    blk.update().eager_inline(dec);
    Func codes = blk.update().hoist_invariants()[0].change_type(Int(32));
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor(ryi, bacc);
    s.reorder(n, ryi).vectorize(n).unroll(ryi).unroll(m);
    Stage cu = codes.update();
    cu.split(rx, rp, rx, chunk).split(rx, rq, ri, 4);
    Func quads = cu.rfactor(rq, q);
    cu.reorder(rq, n).atomic().vectorize(rq).vectorize(n).unroll(m).unroll(u);
    quads.reorder_storage(q, n, m).compute_at(acc, ryo).vectorize(q).vectorize(n).unroll(m).unroll(u).bound_storage(u, ts.interleave);
    quads.update().reorder(ri, q, n, rp, u, m).atomic().vectorize(ri).vectorize(q).vectorize(n).unroll(rp).unroll(u).unroll(m);
    // A piece's act codes then are a lane of a register: by-element sdot
    auto calls = Internal::find_direct_calls(quads.function());
    for (Func a : ac) {
        if (calls.count(a.name())) {
            Func ai = a.in(quads);
            ai.compute_at(quads, u).vectorize(ai.args()[0]);
        }
    }
    for (Func f : {blk, codes}) {
        f.compute_at(acc, ryo).bound_storage(u, ts.interleave).vectorize(n).unroll(m).unroll(u);
    }
    blk.update().vectorize(n).unroll(m).unroll(u);
    acc.compute_at(tile_at(out, st)).vectorize(n).unroll(bacc).unroll(m);
    acc.update().reorder(n, bacc, m, ryo).vectorize(n).unroll(bacc).unroll(m);
}

// The integer path on i8mm smmla (2 x 2 x 8 matrix-multiply tiles): nt x mt
// tiles of 2 x 2 sub-tiles, each 4 dense lanes of the intermediates' storage
// (split_storage), so the tile's accumulators stay in registers.
inline void blocked_mmla(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int block,
                         const std::vector<ApproximationResult> &rs, bool scaled = false) {
    Stage s = tile_out(out, st, dot, cond, ts);
    Var n = dot.args()[0], m = dot.args()[1], nio("nio"), nii("nii"), mio("mio"), mii("mii"), u("u"), bacc("bacc");
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
    // tall tiles: per 2 rows, so few sums live (the weight's decode is CSE'd),
    // unless the codes are scaled (2^k): per block, as there they spill
    for (Func f : {blk, codes}) {
        f.compute_at(LoopLevel(acc, ts.mt > 8 && !scaled ? VarOrRVar(mio) : VarOrRVar(ryo))).bound_storage(u, ts.interleave);
    }
    acc.compute_at(tile_at(out, st));
    sub(acc.update()).reorder(nii, mii, nio, mio, bacc, ryo).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio).unroll(bacc);
    sub(blk.update()).reorder(nii, mii, nio, mio, u).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio).unroll(u);
    sub(codes.update()).reorder(rxk, nii, mii, rxc, nio, mio, u).atomic().vectorize(rxk).vectorize(nii).vectorize(mii).unroll(rxc).unroll(nio).unroll(mio).unroll(u);
}

// The N = M = 1 schedule (GGML's vec_dot). Activations encoded inside the
// pipeline (`staged`) are encoded once, up front.
inline void vec_dot(Func out, const std::vector<Func> &dots, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t) {
    for (Func e : staged) {
        e.compute_root();
    }
    out.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);  // N = M = 1
    blocked(out, 0, dots[0], Expr(), {}, r, blocks[0], blocks[1] == blocks[0], rs, t);
}

// An encoder computed per record (each block of values), vectorized: the
// Funcs with an element per value (e.g. the codes, packed bytes) across their
// elements, and its reductions (e.g. the block's scale) across the block.
inline void encoder(Func e, const std::vector<ApproximationResult> &rs) {
    std::vector<Func> dec = decoders(rs);
    for (const ApproximationResult &res : rs) {
        for (Func f : res.encoded[0].name() == e.name() ? res.intermediates : std::vector<Func>{}) {
            if (f.name() == e.name() || std::any_of(dec.begin(), dec.end(), [&](const Func &d) { return d.name() == f.name(); })) continue;
            if (f.dimensions() > e.dimensions()) {
                f.compute_at(e, e.args()[0]).vectorize(f.args()[0]);
                for (int u = 0; u < f.num_update_definitions(); u++) {
                    f.update(u).vectorize(f.args()[0]);
                }
            } else if (f.has_update_definition()) {
                f.compute_at(e, e.args()[0]).update().atomic().vectorize(f.rvars()[0]);
            }
        }
    }
}

// mul_mat: gemm tiles the outputs (4 x 8 on smmla when the target has i8mm
// and both operands are quantized alike, 4 x 16 for whole records of at least
// 16 rows, 4 x 4 where those waste rows; else 4 x 4 on sdot), in tasks of 32
// x 32 outputs, fewer rows if that leaves under 2 tasks per thread (small N);
// gemv (M = 1) is the vec_dot recipe on row pairs sharing each activation
// block (single rows for N = 1). Edge tiles shift inwards: any N, M at least
// the tile's. With act records of r = blocks[2] > 1 rows, out's first update
// is the rows in whole records (dots[1]) on gemm tiles starting on a record,
// its second the others (dots[0], < r rows) on gemv tiles.
inline void mul_mat(Func out, const std::vector<Func> &dots, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t) {
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    for (Func e : staged) {
        e.compute_root().specialize(M > 1).parallel(e.args().back());  // per activation record
        encoder(e, rs);
    }
    bool integer = blocks[1] == blocks[0], whole = blocks[2] > 1;
    Expr Mr = M / blocks[2] * blocks[2];
    Tiles mmla{4, 16, 1, 32, 32, 2}, mmla8{4, 8, 1, 32, 32, 2}, mmla4{4, 4, 1, 32, 32, 2}, gemm{4, 4, 1, 32, 32, 2}, pairs{2, 1, 2, 16}, rows{1, 1, 2, 16};
    bool mmla_ok = integer && t.has_feature(Target::ARMI8MM);
    // Whole records: the tiles' rows past Mr read clamped records, so one
    // row tile (dot's bound) covers every record, with mmla when there is.
    Func dr = dots.back();
    auto fits = [&](const Tiles &ts, Expr m) { return whole ? N % ts.nt == 0 && N >= ts.nt : N >= ts.nt && m >= ts.mt; };
    // 4-row tiles where the taller ones would compute 4 or 12 rows past Mr,
    // up to 64 rows (measured wins at Mr = 4, 12, 20, 36, 52; losses at 28, 100).
    // A disjunction, so the later branches learn each term false (dr's bound).
    Expr tall = Mr >= mmla.mt, odd_n = !fits(gemm, Mr);
    Expr quad = (Mr % mmla.mt == mmla4.mt && Mr < 4 * mmla.mt) || Mr == mmla8.mt + mmla4.mt;
    if (whole) {  // odd N first, as tall as mmla; the bound is then constant in each branch
        blocked(out, 1, dr, odd_n, {1, mmla_ok ? mmla.mt : gemm.mt, 1, 32, 32, 2}, r, blocks[0], integer, rs, t);
        dr.bound_extent(dr.args()[1], mmla_ok ? select(odd_n, mmla.mt, quad, mmla4.mt, tall, mmla.mt, mmla8.mt) : Expr(gemm.mt));
    }
    if (mmla_ok) {
        if (whole) blocked_mmla(out, whole, dr, quad, mmla4, r, blocks[0], rs);
        if (whole) blocked_mmla(out, whole, dr, tall, mmla, r, blocks[0], rs, blocks[5]);
        blocked_mmla(out, whole, dr, whole ? Expr() : fits(mmla8, M), mmla8, r, blocks[0], rs);
    }
    if (!whole || !mmla_ok) blocked(out, whole, dr, whole ? Expr() : fits(gemm, M), gemm, r, blocks[0], integer, rs, t);
    if (integer && blocks[3] > 1) {  // N is a multiple of the records' rows
        blocked_rows(out, 2 * whole, dots[0], Expr(), {blocks[3], 1, 2, 16}, r, blocks[0], blocks[4], rs);
        return;
    }
    blocked(out, 2 * whole, dots[0], fits(pairs, 1), pairs, r, blocks[0], integer, rs, t);
    blocked(out, 2 * whole, dots[0], Expr(), rows, r, blocks[0], integer, rs, t);
}

}  // namespace ggml
