#pragma once

// Schedules of the mul_mat algorithm (kernels/matmul.cpp). They see its
// structure only: the reduction r (r.x within a block, r.y over the blocks),
// the values per record of each operand, and the operands'
// ApproximationResults.
#include "Halide.h"

#include <functional>

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

// Of the candidate splits ps (each `tasks` tasks of `size` work, plus
// `overhead`: a task's dispatch, ~0.25-1 us on M3), the one whose busiest thread
// does the least, ceil(tasks / threads) * (size + overhead); on ties, the earliest.
inline Expr balanced(const std::vector<int> &ps, float overhead, const std::function<std::pair<Expr, Expr>(int)> &split) {
    Expr T = threads(), best, least;
    for (int p : ps) {
        auto [tasks, size] = split(p);
        Expr load = cast<float>((tasks + T - 1) / T) * (cast<float>(size) + overhead);
        best = best.defined() ? select(load < least, p, best) : Expr(p);
        least = least.defined() ? min(load, least) : load;
    }
    return best;
}

// The tile shape of one mul_mat schedule: nt x mt outputs (N >= nt, M >= mt;
// edge tiles shift inwards; in out's updates, M tails are guarded) share each decoded weight row and activation
// column; `interleave` blocks per iteration into independent accumulators;
// parallel tasks of `rows` x `cols` outputs (rows 0: serial; cols 0: all of M),
// with `tasks`: fewer rows where that balances the threads; with `busy`:
// parallel only where it holds (else a serial loop, cheap when it has no work).
struct Tiles {
    int nt = 1, mt = 1, interleave = 2, rows = 0, cols = 0, tasks = 0;
    Expr busy;
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
inline Stage tile_out(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, bool whole_n = false) {
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
            // tasks of `p` tiles in n, up to rows / nt (on ties, the largest);
            // work in multiply-adds
            Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
            Expr cs = ts.cols ? (M + ts.cols - 1) / ts.cols : 1, tn = (N + ts.nt - 1) / ts.nt;
            Expr tile = cast<float>(ts.nt * (ts.cols ? min(M, ts.cols) : M)) * cast<float>(r.x.extent() * r.y.extent());
            std::vector<int> ps;
            for (int p = ts.rows / ts.nt; p >= 1; p--) {
                ps.push_back(p);
            }
            per = balanced(ps, 1 << 14, [&](int p) { return std::pair{cs * ((tn + p - 1) / p), p * tile}; });
        }
        s.split(no, nc, no, per, TailStrategy::GuardWithIf);
        if (ts.cols) s.reorder(ni, mi, mo, no, mc, nc).fuse(mc, nc, t);
        (ts.busy.defined() ? s.specialize(ts.busy) : s).parallel(ts.cols ? t : VarOrRVar(nc));
    }
    return cond.defined() ? dot.update().specialize(cond) : dot.update();
}

// The terms of e as a sum.
inline void terms(const Expr &e, std::vector<Expr> &ts) {
    if (const auto *a = e.as<Internal::Add>()) {
        terms(a->a, ts);
        terms(a->b, ts);
    } else {
        ts.push_back(e);
    }
}

// Does e (through its Muls and Adds, as distribute() sees it) multiply a sum?
inline bool product_over_sum(const Expr &e) {
    if (const auto *a = e.as<Internal::Add>()) return product_over_sum(a->a) || product_over_sum(a->b);
    const auto *m = e.as<Internal::Mul>();
    return m && (m->a.as<Internal::Add>() || m->b.as<Internal::Add>() || product_over_sum(m->a) || product_over_sum(m->b));
}

inline bool calls_any(const Expr &e, const std::vector<Func> &fs) {
    bool found = false;
    Internal::visit_with(e, [&](auto *self, const Internal::Call *c) {
        for (const Func &f : fs) {
            found |= c->name == f.name();
        }
        self->visit_base(c);
    });
    return found;
}

// Does every term of e call one of fs (no product over a sum to expand)?
inline bool all_terms_call(Expr e, const std::vector<Func> &fs) {
    e = Internal::substitute_in_all_lets(e);
    std::vector<Expr> ts;
    terms(e, ts);
    return !product_over_sum(e) && std::all_of(ts.begin(), ts.end(), [&](const Expr &t) { return calls_any(t, fs); });
}

// Splits f's reduction into the part with the calls to fs and the rest: f
// becomes their sum, each a reduction of its own (so the invariants of each
// then hoist). Returns {f, undefined} if every term calls fs. It is the
// hoist_invariants({fs}) proposed in the review of distribute()
// (halide/Halide#9508), here distribute() then hoist_invariants() (the only
// use of distribute(): swap it for that).
inline std::pair<Func, Func> split_on(Func f, const std::vector<Func> &fs) {
    Expr v = Internal::substitute_in_all_lets(f.update_value());
    const auto *a = v.as<Internal::Add>();
    const auto *self = a ? a->a.as<Internal::Call>() : nullptr;
    if (!self || self->name != f.name()) throw std::invalid_argument("split_on: not a sum reduction: " + f.name());
    if (all_terms_call(a->b, fs)) return {f, Func()};
    if (product_over_sum(a->b)) f.update().distribute();
    Func with, rest;
    for (Func h : f.update().hoist_invariants()) {
        Func &part = calls_any(h.update_value(), fs) ? with : rest;
        if (part.defined()) throw std::invalid_argument("split_on: several terms of each part: " + f.name());
        part = h;
    }
    return {with, rest};
}

// A reduction's value read from a field its operand's format stores (q8_1's
// block sums): encode reads the field (computed offline, in the field's
// precision), decode takes it as read.
struct Stored {
    Func field;
    std::vector<Func> encode(const std::vector<Func> &) const {
        return {field};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return e;
    }
};

// The weight's decode with terms free of its codes (q4_1's codes * d + min):
// dot (on out's stage st) splits into the part with the codes, returned for
// the recipes (whose codes' dot then is integer as for q4_0), and the rest,
// per block the code-free factor (the min) times the block's sum of act
// values: `sum` (the act's stored sums, q8_1's s) where defined, else summed.
// Otherwise returns dot.
inline Func affine(Func out, int st, Func dot, const RDom &r, Func sum, const std::vector<ApproximationResult> &rs) {
    std::vector<Func> floats, codes;
    for (Func f : rs[0].decode_funcs()) {
        (f.type().is_float() ? floats : codes).push_back(f);
    }
    Func w = rs[0].replacement, probe;
    probe(w.args()) = w(w.args());
    Stage(probe).eager_inline(floats);
    if (all_terms_call(probe.value(), codes)) return dot;
    dot.update().eager_inline(floats);
    auto [with, rest] = split_on(dot, codes);
    // per block, the tile's outputs at once (n vectorized, m unrolled: the
    // tiles are constant)
    LoopLevel at = tile_at(out, st);
    Var n = dot.args()[0], m = dot.args()[1], u("u");
    Func per = rest.update().rfactor(r.y, u), s = per.update().hoist_invariants()[0];
    Expr sv = s.update_value();
    if (sum.defined() && sv.as<Internal::Add>()->b.as<Internal::Call>()) {
        Func field("stored_sum");
        field(n, m, u) = sum(u, m);
        s.approximate_by(Stored{field}, {per});
    } else {
        s.update().eager_inline(decoders(rs));
        s.compute_at(rest, r.y).unroll(n).unroll(m).update().atomic().vectorize(r.x).unroll(n).unroll(m);
    }
    for (Func f : {dot, rest, per}) {
        f.compute_at(f.name() == per.name() ? LoopLevel(rest, r.y) : at).vectorize(n).unroll(m);
        f.update().vectorize(n).unroll(m);
    }
    rest.update().reorder(n, m, r.y);
    return with;
}

// The weight quantized in blocks: per block (r.y) and output a dot of the
// codes (when the activation is quantized alike, an int32 sdot: 4 products per
// lane; otherwise f32) times the hoisted scales, accumulated per lane in f32
// (GGML's order), on ts tiles of out under `cond`.
inline void blocked(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), u("u"), bacc("bacc");
    RVar ry = r.y, rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    int per = integer ? 4 : 1, vec = t.natural_vector_size<int8_t>();
    s.split(r.x, rxc, rxo, vec).split(rxo, rxo, rxi, per);
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
// across the rows. On gemm tiles of more than `group` (the act's rows per
// record) rows, as KleidiAI's dotprod gemm: the sums per act record, its
// codes staged in memory order (a 4-code piece of each row per lane), and its
// scales as one vector.
inline void blocked_rows(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int chunk,
                         const std::vector<ApproximationResult> &rs, int group = 0) {
    Stage s = tile_out(out, st, dot, cond, ts, r, true);
    Var n = dot.args()[0], m = dot.args()[1], q("q"), u("u"), bacc("bacc");
    // ryi_rows: not ryi, as dot's specializations share bounds by RVar name (gap M)
    RVar ry = r.y, rx = r.x, rp("rp"), rq("rq"), ri("ri"), ryo("ryo"), ryi("ryi_rows");
    Func blk = s.rfactor(ry, u);
    // The act's codes (integer decode Funcs of rs[1..]; rs[0] is the weight's)
    // stay out of the inline: staged per block in registers, below; on gemm
    // tiles, so do its scales (float, per block and row).
    bool grouped = group && ts.mt > group;
    std::vector<Func> dec, ac, as;
    for (Func f : decoders(rs)) {
        bool act = false;
        for (size_t i = 1; i < rs.size(); i++) {
            for (const Func &g : rs[i].decode_trace.intermediates) {
                act |= g.name() == f.name();
            }
        }
        if (act && f.type().is_int()) {
            ac.push_back(f);
        } else if (act && grouped && f.type().is_float() && f.dimensions() == 2) {
            as.push_back(f);
        } else {
            dec.push_back(f);
        }
    }
    blk.update().eager_inline(dec);
    Func codes = blk.update().hoist_invariants()[0].change_type(Int(32));
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor(ryi, bacc);
    Var mo("mog"), mi("mig");
    LoopLevel at(acc, grouped ? VarOrRVar(mo) : VarOrRVar(ryo));
    s.reorder(n, ryi).vectorize(n).unroll(ryi).unroll(m);
    Stage cu = codes.update();
    cu.split(rx, rp, rx, chunk).split(rx, rq, ri, 4);
    Func quads = cu.rfactor(rq, q);
    cu.reorder(rq, n).atomic().vectorize(rq).vectorize(n).unroll(m).unroll(u);
    quads.reorder_storage(q, n, m).compute_at(at).vectorize(q).vectorize(n).unroll(m).unroll(u).bound_storage(u, ts.interleave);
    // gemm tiles: per act record, piece-major, each piece of its codes
    // staged just before use (the record's sums the only ones live)
    Stage qu = quads.update();
    grouped ? qu.split(m, mo, mi, group).reorder(ri, q, n, mi, rp, mo, u).unroll(mi).unroll(mo) : qu.reorder(ri, q, n, rp, u, m).unroll(m);
    qu.atomic().vectorize(ri).vectorize(q).vectorize(n).unroll(rp).unroll(u);
    // A piece's act codes then are a lane of a register: by-element sdot
    auto calls = Internal::find_direct_calls(quads.function());
    for (Func a : ac) {
        if (calls.count(a.name())) {
            Func ai = a.in(quads);
            Var k = ai.args()[0];
            if (grouped && ai.dimensions() == 3) {  // (code, block, row)
                Var b = ai.args()[1], am = ai.args()[2], ko("ko"), ki("ki"), kv("kv"), sko, ski;
                ai.split_storage(k, sko, ski, 4).reorder_storage(ski, am, sko, b);
                ai.split(k, ko, ki, 4).reorder(ki, am, ko).fuse(ki, am, kv).unroll(ko);
                k = kv;
            }
            ai.compute_at(grouped ? LoopLevel(quads, rp) : LoopLevel(quads, u)).vectorize(k);
        }
    }
    for (Func f : {blk, codes}) {
        f.compute_at(at).bound_storage(u, ts.interleave).vectorize(n).unroll(m).unroll(u);
    }
    blk.update().vectorize(n).unroll(m).unroll(u);
    // the record's scales: one load, each row's a lane (fmla by element)
    auto bcalls = Internal::find_direct_calls(blk.function());
    for (Func a : as) {
        if (bcalls.count(a.name())) {
            Func ai = a.in(blk);
            // rows innermost: the block's extent is a guarded min(), not 1
            ai.reorder_storage(ai.args()[1], ai.args()[0]).compute_at(at).vectorize(ai.args()[1]);
        }
    }
    acc.compute_at(tile_at(out, st)).vectorize(n).unroll(bacc).unroll(m);
    Stage au = acc.update();
    grouped ? au.split(m, mo, mi, group).reorder(n, bacc, mi, mo, ryo).unroll(mi).unroll(mo) : au.reorder(n, bacc, m, ryo).unroll(m);
    au.vectorize(n).unroll(bacc);
}

// The integer path on i8mm smmla (2 x 2 x 8 matrix-multiply tiles): nt x mt
// tiles of 2 x 2 sub-tiles, each 4 dense lanes of the intermediates' storage
// (split_storage), so the tile's accumulators stay in registers.
inline void blocked_mmla(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r,
                         const std::vector<ApproximationResult> &rs, bool scaled = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], nio("nio"), nii("nii"), mio("mio"), mii("mii"), u("u"), bacc("bacc");
    RVar ry = r.y, rxc("rxc"), rxk("rxk"), ryo("ryo"), ryi("ryi");
    s.split(r.x, rxc, rxk, 8);
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
// pipeline (`staged`) are encoded once, up front. sums[i]: the stored block
// sums of dots[i]'s act, if its format has them.
inline void vec_dot(Func out, const std::vector<Func> &dots, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const std::vector<Func> &sums, const Target &t) {
    for (Func e : staged) {
        e.compute_root();
    }
    out.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);  // N = M = 1
    blocked(out, 0, affine(out, 0, dots[0], r, sums[0], rs), Expr(), {}, r, blocks[1] == blocks[0], rs, t);
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

// Metal (Apple GPUs), as GGML's kernel_mul_mv_q4_0_f32: threadgroups of `sg`
// simdgroups (gpu_threads), each `nr` rows of one column of out. A lane takes
// half a block (one of its two 8-byte halves: 16 values) of every 16th block,
// accumulating the rows in registers (the codes' sum per block, then times
// its scale); the lanes' sums then reduce across the simdgroup (an atomic
// update over the gpu lanes: simd shuffles) and lane 0 writes. For M > 1,
// each simdgroup takes rows x cols of out (up to 8 columns, fewer rows for
// more), a lane per value of every block (coalesced loads), its products in
// registers (each decoded weight used cols times, each act value rows times),
// reduced the same way (the gemv lanes' act loads, 16 values of blocks 16
// apart, are strided: far slower for that many columns). Each kernel has both
// trees, behind uniform flags (dot's specialization, done on the host).
inline void metal_rows(Func out, Func dot, const RDom &r, const std::vector<ApproximationResult> &rs, int nr = 4, int sg = 2) {
    Var n = out.args()[0], m = out.args()[1], no("no"), ns("ns"), ni("ni"), mo("mo"), mi("mi"), uh("uh"), ub("ub"), u("u"), l("l");
    RVar rx = r.x, ry = r.y, rxn("rxn"), rxh("rxh"), rxi("rxi"), ryo("ryo"), ryi("ryi"), rl("rl");
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    auto tile = [&](Stage o, int rows, int groups, int cols) {
        o.split(n, no, ni, rows * groups, TailStrategy::ShiftInwards).split(ni, ns, ni, rows);
        if (cols > 1) {
            o.split(m, mo, mi, cols, TailStrategy::ShiftInwards).reorder(ni, mi, ns, no, mo).unroll(mi).gpu_blocks(no, mo);
        } else {
            o.reorder(ni, ns, no, m).gpu_blocks(no, m);
        }
        o.unroll(ni).gpu_threads(ns);
    };
    Stage big = Stage(out).specialize(N >= nr * sg);
    for (int cols = 8; cols > 1; cols--) {
        tile(big.specialize(M >= cols), cols > 4 ? 2 : 4, sg, cols);
    }
    tile(big, nr, sg, 1);
    tile(Stage(out), 1, 1, 1);  // N < nr * sg: a row per threadgroup
    dot.compute_at(out, ni);    // per row: warp allocations of one value
    Var lx("lx");
    RVar ryk("ryk"), ryu("ryu");
    Stage sm = dot.update().specialize(M >= 2);
    Func lanes = sm.rfactor(rx, lx);
    sm.atomic().gpu_lanes(rx);
    lanes.compute_at(out, ns).gpu_lanes(lx).unroll(n).unroll(m);
    lanes.update().split(ry, ryk, ryu, 4, TailStrategy::GuardWithIf).reorder(n, m, ryu, ryk, lx).gpu_lanes(lx).unroll(n).unroll(m).unroll(ryu);
    Stage s = dot.update();
    // value j = rxn * 16 + rxh * 8 + rxi: the low (rxn 0) and high nibbles of byte rxh * 8 + rxi
    s.split(rx, rxn, rx, 16).split(rx, rxh, rxi, 8);
    Func blk = s.rfactor({{rxh, uh}, {ry, u}});
    blk.update().eager_inline(decoders(rs));
    Func codes = blk.update().hoist_invariants()[0];
    s.split(ry, ryo, ryi, 16, TailStrategy::GuardWithIf);
    Func acc = s.rfactor({{rxh, uh}, {ryi, ub}});
    s.fuse(rxh, ryi, rl).atomic().gpu_lanes(rl);
    acc.reorder_storage(uh, ub, n, m).compute_at(out, ns);
    acc.fuse(uh, ub, l).reorder(n, m, l).gpu_lanes(l).unroll(n);
    acc.update().fuse(uh, ub, l).reorder(n, m, ryo, l).gpu_lanes(l).unroll(n);
    // store_at: works around halide/Halide#9571; bound_storage: also in the K tail: registers
    for (Func f : {blk, codes}) {
        f.compute_at(acc, ryo).store_at(acc, l).bound_storage(u, 1).unroll(f.args()[0]);
    }
    blk.update().unroll(blk.args()[0]);
    codes.update().reorder(codes.args()[0], rxi, rxn).unroll(codes.args()[0]).unroll(rxi).unroll(rxn);
}

// mul_mat: gemm tiles the outputs (4 x 8 on smmla when the target has i8mm
// and both operands are quantized alike, 4 x 16 for whole records of at least
// 16 rows, 4 x 4 where those waste rows; on weight records in 4-code pieces,
// those tiles on by-element sdot; else 4 x 4 on sdot), in tasks of up to 32
// x 32 outputs, fewer rows where that balances the threads (small N);
// gemv (M = 1) is the vec_dot recipe on row pairs sharing each activation
// block (single rows for N = 1). Affine weights split first, as in vec_dot.
// Edge tiles shift inwards: any N, M at least the tile's. With act records of r = blocks[2] > 1 rows, out's first update
// is the rows in whole records (dots[1]) on gemm tiles starting on a record,
// its second the others (dots[0], < r rows) on gemv tiles.
inline void mul_mat(Func out, const std::vector<Func> &reductions, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const std::vector<Func> &sums, const Target &t) {
    bool integer = blocks[1] == blocks[0], whole = blocks[2] > 1;
    std::vector<Func> dots = reductions;
    if (t.has_feature(Target::Metal)) {
        if (!staged.empty() || blocks[2] > 1 || blocks[3] > 1) throw std::invalid_argument("metal: act formats stored as computed, weights one row per record");
        if (affine(out, 0, dots[0], r, sums[0], rs).name() != dots[0].name()) throw std::invalid_argument("metal: weights decoded as codes times scales");
        metal_rows(out, dots[0], r, rs);
        return;
    }
    for (size_t i = 0; i < dots.size(); i++) {
        dots[i] = affine(out, i ? 1 : 2 * whole, dots[i], r, sums[i], rs);
    }
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    Expr Mr = M / blocks[2] * blocks[2];
    for (size_t i = 0; i < staged.size(); i++) {
        // staged[i] encodes dots[i]'s act. Records in tasks of up to 16 chunks
        // of their blocks (work in values), so even a few records (small M)
        // spread over the threads. The rows past Mr (staged[0] for whole
        // records), up to 3 and often none, one per task (the fused loop would
        // encode a row even for none).
        Func e = staged[i];
        Var b = e.args()[0], rec = e.args().back(), bo("bo"), t("t");
        Stage s = e.compute_root().specialize(M > 1);
        if (whole && !i) {
            s.parallel(rec);
        } else {
            Expr B = r.y.extent(), recs = whole ? Mr / blocks[2] : M;
            std::vector<int> cs;
            for (int c = 1; c <= 16; c++) {
                cs.push_back(c);
            }
            Expr chunks = balanced(cs, 4096, [&](int c) { return std::pair{recs * c, (B + c - 1) / c * blocks[1] * blocks[2]}; });
            s.split(b, bo, b, (B + chunks - 1) / chunks, TailStrategy::GuardWithIf).fuse(rec, bo, t).parallel(t);
        }
        encoder(e, rs);
    }
    Tiles mmla{4, 16, 1, 32, 32, 2}, mmla8{4, 8, 1, 32, 32, 2}, mmla4{4, 4, 1, 32, 32, 2}, gemm{4, 4, 1, 32, 32, 2}, pairs{2, 1, 2, 16}, rows{1, 1, 2, 16};
    bool mmla_ok = integer && t.has_feature(Target::ARMI8MM);
    // Weight records in 4-code pieces: by-element sdot on the same tiles (on M3,
    // KleidiAI's dotprod gemm beats both i8mm ones)
    bool lanes = whole && integer && blocks[3] > 1 && blocks[4] == 4, tiled = mmla_ok || lanes;
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
        blocked(out, 1, dr, odd_n, {1, tiled ? mmla.mt : gemm.mt, 1, 32, 32, 2}, r, integer, rs, t);
        dr.bound_extent(dr.args()[1], tiled ? select(odd_n, mmla.mt, quad, mmla4.mt, tall, mmla.mt, mmla8.mt) : Expr(gemm.mt));
    }
    if (lanes) {
        for (auto [c, ts] : std::vector<std::pair<Expr, Tiles>>{{quad, mmla4}, {tall, mmla}, {Expr(), mmla8}}) {
            blocked_rows(out, whole, dr, c, ts, r, blocks[4], rs, blocks[2]);
        }
    } else if (mmla_ok) {
        if (whole) blocked_mmla(out, whole, dr, quad, mmla4, r, rs);
        if (whole) blocked_mmla(out, whole, dr, tall, mmla, r, rs, blocks[5]);
        blocked_mmla(out, whole, dr, whole ? Expr() : fits(mmla8, M), mmla8, r, rs);
    }
    if (!whole || !tiled) blocked(out, whole, dr, whole ? Expr() : fits(gemm, M), gemm, r, integer, rs, t);
    // The rows past Mr (fewer than a record's): no tasks when there are none.
    Expr rest = whole ? M % blocks[2] != 0 : Expr();
    if (integer && blocks[3] > 1) {  // N is a multiple of the records' rows
        blocked_rows(out, 2 * whole, dots[0], Expr(), {blocks[3], 1, 2, 16, 0, 0, rest}, r, blocks[4], rs);
        return;
    }
    pairs.busy = rows.busy = rest;
    blocked(out, 2 * whole, dots[0], fits(pairs, 1), pairs, r, integer, rs, t);
    blocked(out, 2 * whole, dots[0], Expr(), rows, r, integer, rs, t);
}

}  // namespace ggml
