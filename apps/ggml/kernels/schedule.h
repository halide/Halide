#pragma once

// Schedules of the mul_mat algorithm (kernels/matmul.cpp). They see its
// structure only: the reduction r (r.x within a block, r.y over the blocks),
// the values per record of each operand, and the operands'
// ApproximationResults.
#include "Halide.h"

#include <functional>

namespace ggml {

using namespace Halide;

// A decode Func's constant table (a Buffer it reads), if any.
inline Buffer<> table_of(Func f) {
    Buffer<> b;
    for (const Expr &v : f.function().definition().values()) {
        Internal::visit_with(v, [&](auto *self, const Internal::Call *c) {
            if (c->call_type == Internal::Call::Image && c->image.defined()) b = c->image;
            self->visit_base(c);
        });
    }
    return b;
}

// The decode Funcs eager_inline can fold into a consumer, of rs[first, last):
// all, or only the scales (float) or the codes (integer); or the ones reading
// a table (`Of::Tables`), which a vectorized inline would gather from: see
// tables().
enum class Of { All,
                Floats,
                Ints,
                Tables };
inline std::vector<Func> decoders(const std::vector<ApproximationResult> &rs, Of of = Of::All, size_t first = 0, size_t last = SIZE_MAX) {
    std::vector<Func> fs;
    for (size_t i = first; i < std::min(last, rs.size()); i++) {
        for (Func f : rs[i].decode_funcs()) {
            if ((of == Of::Tables) != table_of(f).defined()) continue;
            if (of == Of::All || of == Of::Tables || (of == Of::Floats) == f.type().is_float()) fs.push_back(f);
        }
    }
    return fs;
}

// Every stage of f in registers: `vec` vectorized, `unrolled` unrolled, each
// where the stage has it.
inline Func regs(Func f, const std::vector<VarOrRVar> &vec, const std::vector<VarOrRVar> &unrolled) {
    for (int i = 0; i <= f.num_update_definitions(); i++) {
        Stage s = i ? f.update(i - 1) : Stage(f);
        auto has = [&](const VarOrRVar &v) {
            const auto &ds = s.get_schedule().dims();
            // as Func.cpp's var_name_match: a split's dims are "parent.name"
            return std::any_of(ds.begin(), ds.end(), [&](const Internal::Dim &d) {
                return d.var == v.name() || Internal::ends_with(d.var, "." + v.name());
            });
        };
        for (const VarOrRVar &v : vec) {
            if (has(v)) s.vectorize(v);
        }
        for (const VarOrRVar &v : unrolled) {
            if (has(v)) s.unroll(v);
        }
    }
    return f;
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
// `whole_n`: N is a multiple of ts.nt. `guard_m`: tiles in m from 0 mod
// ts.mt, the last guarded. Returns dot's update under `cond`.
inline Stage tile_out(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, bool whole_n = false, bool guard_m = false) {
    VarOrRVar n = out.args()[0], m = row_var(out, st), mo = row_var(out, st, "mo"), mi = row_var(out, st, "mi"), mc = row_var(out, st, "mc"), t = row_var(out, st, "nc");
    Var no("no"), ni("ni"), nc("nc");
    Stage o = st ? out.update(st - 1) : Stage(out), s = cond.defined() ? o.specialize(cond) : o;
    // Updates: whole tiles in n (cond knows N % nt == 0); m an RVar.
    s.split(n, no, ni, ts.nt, st || whole_n ? TailStrategy::RoundUp : TailStrategy::ShiftInwards);
    s.split(m, mo, mi, ts.mt, st || guard_m ? TailStrategy::GuardWithIf : TailStrategy::ShiftInwards);
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

// Which calls a split is on: of the Funcs fs (by name), or any call `is` picks.
using CallTest = std::function<bool(const Internal::Call *)>;
inline CallTest call_of(const std::vector<Func> &fs) {
    return [fs](const Internal::Call *c) {
        return std::any_of(fs.begin(), fs.end(), [&](const Func &f) { return c->name == f.name(); });
    };
}

inline bool calls_any(const Expr &e, const CallTest &is) {
    bool found = false;
    Internal::visit_with(e, [&](auto *self, const Internal::Call *c) {
        found |= is(c);
        self->visit_base(c);
    });
    return found;
}
inline bool calls_any(const Expr &e, const std::vector<Func> &fs) {
    return calls_any(e, call_of(fs));
}

// Whether a kernel decodes bit fields through constant tables (Tabulate): on
// CPUs. A table moves the decode off the SIMD units, which bound the dot
// (Apple M3 Max, q5_0, one thread, medians over link layouts: vec_dot from
// 0.80x GGML's speed to 1.01x, gemv on single rows from 0.86x to 1.0x, gemm
// M = 32 from 2.2x to 2.7x).
inline bool use_tables(const Target &t) {
    return !t.has_gpu_feature();
}

// The table decodes at `at` (per block group), each a copy for its calls from
// `consumers` (a pipeline computes them per kernel): unrolled over all but
// their first dimension, split by the table's rows, so each read of a row is
// a dense load.
inline void tables(const std::vector<ApproximationResult> &rs, const std::vector<Func> &consumers, LoopLevel at) {
    for (Func f : decoders(rs, Of::Tables)) {
        std::vector<Func> cs;
        for (Func c : consumers) {
            if (!c.defined()) continue;
            bool calls = false;
            const Internal::Function &fn = c.function();
            for (const Expr &v : fn.definition().values()) {
                calls |= calls_any(v, {f});
            }
            for (const Internal::Definition &u : fn.updates()) {
                for (const Expr &v : u.values()) {
                    calls |= calls_any(v, {f});
                }
            }
            if (calls) cs.push_back(c);
        }
        if (cs.empty()) continue;
        Func g = f.clone_in(cs);
        Var j = g.args()[0], jo("jo"), ji("ji");
        g.compute_at(at).split(j, jo, ji, table_of(f).dim(0).extent()).vectorize(ji).unroll(jo);
        for (size_t k = 1; k < g.args().size(); k++) {
            g.unroll(g.args()[k]);
        }
    }
}

// Does every term of e make such a call (no product over a sum to expand)?
inline bool all_terms_call(Expr e, const CallTest &is) {
    e = Internal::substitute_in_all_lets(e);
    std::vector<Expr> ts;
    terms(e, ts);
    return !product_over_sum(e) && std::all_of(ts.begin(), ts.end(), [&](const Expr &t) { return calls_any(t, is); });
}

// Splits f's reduction into the part with the calls to fs and the rest: f
// becomes their sum, each a reduction of its own (so the invariants of each
// then hoist). Returns {f, undefined} if every term calls fs. It is the
// hoist_invariants({fs}) proposed in the review of distribute()
// (halide/Halide#9508), here distribute() then hoist_invariants() (the only
// use of distribute(): swap it for that).
inline std::pair<Func, Func> split_on(Stage st, Expr v, const std::string &name, const CallTest &fs) {
    v = Internal::substitute_in_all_lets(v);
    const auto *a = v.as<Internal::Add>();
    const auto *self = a ? a->a.as<Internal::Call>() : nullptr;
    if (!self || self->name != name) throw std::invalid_argument("split_on: not a sum reduction: " + name);
    if (all_terms_call(a->b, fs)) return {Func(), Func()};
    if (product_over_sum(a->b)) st.distribute();
    Func with, rest;
    for (Func h : st.hoist_invariants()) {
        Func &part = calls_any(h.update_value(), fs) ? with : rest;
        if (part.defined()) throw std::invalid_argument("split_on: several terms of each part: " + name);
        part = h;
    }
    return {with, rest};
}
inline std::pair<Func, Func> split_on(Stage st, Expr v, const std::string &name, const std::vector<Func> &fs) {
    return split_on(st, std::move(v), name, call_of(fs));
}
inline std::pair<Func, Func> split_on(Func f, const std::vector<Func> &fs) {
    auto parts = split_on(f.update(), f.update_value(), f.name(), fs);
    return parts.first.defined() ? parts : std::pair{f, Func()};
}

// The value of f's first update under `cond` (its specialization; all of it
// if undefined), as transformed so far.
inline Expr update_value(Func f, const Expr &cond) {
    const Internal::Definition &d = f.function().update(0);
    if (!cond.defined()) return d.values()[0];
    for (const Internal::Specialization &sp : d.specializations()) {
        if (Internal::equal(sp.condition, cond)) return sp.definition.values()[0];
    }
    throw std::invalid_argument("update_value: no specialization of " + f.name());
}

// Does the weight's decode have terms free of its codes (q4_1's codes * d +
// min)? Its codes: its integer decode Funcs, or integer ports read as is
// (e.g. planar int8 codes).
inline bool affine(const std::vector<ApproximationResult> &rs) {
    Func w = rs[0].replacement, probe;
    probe(w.args()) = w(w.args());
    Stage(probe).eager_inline(decoders(rs, Of::Floats, 0, 1));
    Expr e = Internal::substitute_in_all_lets(probe.value());
    std::vector<Expr> ts;
    terms(e, ts);
    return product_over_sum(e) || !std::all_of(ts.begin(), ts.end(), [](const Expr &t) {
               bool code = false;
               Internal::visit_with(t, [&](auto *self, const Internal::Call *c) {
                   code |= (c->call_type == Internal::Call::Halide || c->call_type == Internal::Call::Image) && c->type.is_int_or_uint();
                   self->visit_base(c);
               });
               return code;
           });
}

// Does f's increment (a product) have a factor free of its RVars?
inline bool invariant_factor(Func f) {
    Expr v = Internal::substitute_in_all_lets(f.update_value());
    const auto *a = v.as<Internal::Add>();
    if (!a) return false;
    std::vector<Expr> fs{a->b};
    for (size_t i = 0; i < fs.size(); i++) {
        if (const auto *m = fs[i].as<Internal::Mul>()) {
            fs.push_back(m->a);
            fs.push_back(m->b);
            continue;
        }
        bool free = true;
        Internal::visit_with(fs[i], [&](auto *, const Internal::Variable *x) { free &= !x->reduction_domain.defined(); });
        if (free && !Internal::is_const(fs[i])) return true;
    }
    return false;
}

// blk's per-block dot (r.y rfactored to a pure var) with the decoders inlined
// and the scales hoisted: returns the dot of the codes (int32 if `integer`).
// Affine weights first split on the weight's codes, the rest returned in
// `rest` (else undefined): the code-free factor (the min) times the block's
// act sum, of its codes (int32) if `integer`, else of its values. A factor
// common to both (the act's scale) hoists first: blk is then it times `mid`
// (else undefined), the sum split, so one product per block reaches blk.
// `aff`: blk has the affine weight's code-free terms (affine(rs), unless
// derived_sums() took them out).
inline Func block_dot(Func blk, const std::vector<ApproximationResult> &rs, bool integer, Func &rest, Func &mid, bool aff) {
    Func codes;
    rest = mid = Func();
    if (!aff) {
        blk.update().eager_inline(decoders(rs));
        codes = blk.update().hoist_invariants()[0];
    } else {
        blk.update().eager_inline(decoders(rs, Of::Floats));
        if (invariant_factor(blk)) mid = blk.update().hoist_invariants()[0];
        std::tie(codes, rest) = split_on(mid.defined() ? mid : blk, decoders(rs, Of::Ints, 0, 1));
        for (Func f : {codes, rest}) {
            f.update().eager_inline(decoders(rs, Of::Ints));
        }
        if (integer) rest = rest.change_type(Int(32));
    }
    return integer ? codes.change_type(Int(32)) : codes;
}

// An affine weight's code-free terms, read from the act's derived port where
// one is their act part: dot's increment (s, out's stage st tile under
// `cond`) splits on the weight's codes (split_on) into `with`, computed per
// tile, s then its update, and the rest, d.sums, the weight's min times the
// act; per block (r.y rfactored to u, d.blk), the min hoists out of the act's
// block sum, which is then replaced by a read of the first derived port whose
// definition it matches (Func::approximate_by(DerivedPort, ...): e.g. q8_1's
// s; d.sum, if none matches). Returns false (dot untouched) if the weight is
// not affine or the act has no derived port.
struct Sums {
    Func sums, blk, sum;
};
inline bool derived_sums(Func out, int st, Func dot, Stage &s, const Expr &cond, const RDom &r, const std::vector<ApproximationResult> &rs, Sums &d) {
    d = Sums();
    if (rs.size() < 2 || rs[1].derived_ports().empty() || !affine(rs)) return false;
    s.eager_inline(decoders(rs, Of::Floats, 0, 1));
    Func with;
    std::tie(with, d.sums) = split_on(s, update_value(dot, cond), dot.name(), decoders(rs, Of::Ints, 0, 1));
    if (!with.defined() || !d.sums.defined()) throw std::invalid_argument("derived_sums: no code-free terms in " + dot.name());
    d.blk = d.sums.update().rfactor(r.y, Var("u"));
    d.sum = d.blk.update().hoist_invariants()[0];
    for (const DerivedPort &p : rs[1].derived_ports()) {
        if (match_derived_port(d.sum, p)) {
            d.sum.approximate_by(p, {d.blk});
            d.sum = Func();
            break;
        }
    }
    with.compute_at(tile_at(out, st));
    s = with.update();
    return true;
}

// d's sums alongside acc's update (at ryo), its blocks per ryo. On gemm
// tiles, the rows' mins vectorized (n), times each act column's port, a chain
// per column; on gemv tiles, per block, the min times the act's port, one
// accumulator per interleaved block (as acc: one fused-multiply-add each on
// the loop-carried chains).
inline void schedule_sums(Sums d, Func out, int st, Func acc, Var n, Var m, const Tiles &ts, const RDom &r, bool gemm, int vec) {
    if (!d.sums.defined()) return;
    Var u("u"), v("v");
    RVar ry = r.y, ryo("ryo"), ryi("ryi");
    LoopLevel tile = tile_at(out, st), blocks(acc, ryo);
    d.sums.compute_at(tile);
    d.sums.update().split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    if (gemm) {
        d.sums.vectorize(n).unroll(m);
        d.sums.update().reorder(n, m, ryi, ryo).vectorize(n).unroll(m).unroll(ryi).compute_with(acc.update(), ryo);
        regs(d.blk, {n}, {u, m});
    } else {
        Func spart = d.sums.update().rfactor(ryi, v);
        d.sums.update().unroll(ryi).unroll(n).unroll(m);
        regs(spart.compute_at(tile), {}, {v, n, m});
        spart.update().reorder(v, n, m, ryo).compute_with(acc.update(), ryo);
        regs(d.blk, {}, {u, n, m});
    }
    d.blk.compute_at(blocks).bound_storage(u, ts.interleave);
    if (d.sum.defined()) {  // no port matched: the act's block sums
        d.sum.compute_at(blocks).unroll(u).unroll(n).unroll(m);
        d.sum.update().atomic().vectorize(r.x, vec);
    }
}

// The weight quantized in blocks: per block (r.y) and output a dot of the
// codes (when the activation is quantized alike, an int32 sdot: 4 products per
// lane; otherwise f32) times the hoisted scales, accumulated per lane in f32
// (GGML's order), on ts tiles of out under `cond`. `derived`: an affine
// weight's code-free terms read the act's derived port (derived_sums()), a
// product per block accumulated alongside.
inline void blocked(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t, bool derived = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), u("u"), bacc("bacc");
    RVar ry = r.y, rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    bool aff = affine(rs);
    Sums d;
    if (derived && derived_sums(out, st, dot, s, cond, r, rs, d)) aff = false;
    int per = integer ? 4 : 1, vec = t.natural_vector_size<int8_t>();
    s.split(r.x, rxc, rxo, vec).split(rxo, rxo, rxi, per);
    Func blk = s.rfactor({{rxo, lane}, {ry, u}});
    blk.bound(lane, 0, vec / per);  // the lane's range, so the block's scales hoist
    Func rest, mid, codes = block_dot(blk, rs, integer, rest, mid, aff);
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor({{rxo, lane}, {ryi, bacc}});
    Func lanes = s.rfactor(rxo, lane);
    s.atomic().vectorize(rxo).unroll(n).unroll(m);
    acc.update().reorder(lane, bacc, n, m, ryo);
    // rfactor put the lanes last in storage; per block group (ryo), the
    // blocks' storage also in the K tail: on the stack
    for (Func f : {lanes, acc}) {
        regs(f.compute_at(tile_at(out, st)).reorder_storage(lane, n, m), {lane}, {ryi, bacc, n, m});
    }
    for (Func f : {blk, mid, codes}) {
        if (f.defined()) regs(f.compute_at(acc, ryo).reorder_storage(lane, n, m).bound_storage(u, ts.interleave), {lane}, {u, n, m});
    }
    if (rest.defined()) {
        // the act's block sum, once per act column: the rows' copies are a
        // broadcast (n vectorized)
        regs(rest.compute_at(acc, ryo).reorder_storage(lane, n, m).bound_storage(u, ts.interleave), {lane}, {u, m});
        ts.nt > 1 ? regs(rest, {n}, {}) : regs(rest, {}, {n});
    }
    for (Func f : {codes, rest}) {
        if (f.defined()) f.update().atomic().vectorize(rxi).unroll(rxc);
    }
    schedule_sums(d, out, st, acc, n, m, ts, r, ts.nt > 2, vec);
    tables(rs, {blk, mid, codes, rest}, LoopLevel(acc, ryo));
}

// Weights of one value per record (floats: no blocks, r.x is a single value):
// per output, the products accumulated in f32 in vectors of `vec` values of
// r.y (2 f32 vectors: one of 16-bit floats), ts.interleave independent ones
// (the K tail guarded), then summed across them, on ts tiles of out under
// `cond`. The decoders (conversions to f32) are inlined.
inline void flat(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, const Target &t) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), bacc("bacc");
    RVar rx = r.x, ry = r.y, ryo("ryo"), ryi("ryi"), ryl("ryl");
    int vec = 2 * t.natural_vector_size<float>();
    s.split(ry, ryo, ryi, vec * ts.interleave, TailStrategy::GuardWithIf).split(ryi, ryi, ryl, vec);
    Func acc = s.rfactor({{ryl, lane}, {ryi, bacc}});
    Func lanes = s.rfactor(ryl, lane);
    s.atomic().vectorize(ryl).unroll(n).unroll(m);
    acc.update().reorder(rx, lane, bacc, n, m, ryo);
    for (Func f : {lanes, acc}) {
        regs(f.compute_at(tile_at(out, st)).reorder_storage(lane, n, m), {lane}, {ryi, bacc, n, m});
    }
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
    if (affine(rs)) throw std::invalid_argument("blocked_rows: weights decoded as codes times scales");
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
    cu.reorder(rq, n).atomic().vectorize(rq);
    // gemm tiles: per act record, piece-major, each piece of its codes
    // staged just before use (the record's sums the only ones live)
    Stage qu = quads.update();
    grouped ? qu.split(m, mo, mi, group).reorder(ri, q, n, mi, rp, mo, u) : qu.reorder(ri, q, n, rp, u, m);
    qu.atomic().vectorize(ri);
    regs(quads.reorder_storage(q, n, m).compute_at(at).bound_storage(u, ts.interleave), {q, n}, {m, mi, mo, rp, u});
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
        regs(f.compute_at(at).bound_storage(u, ts.interleave), {n}, {m, u});
    }
    // the record's scales: one load, each row's a lane (fmla by element)
    auto bcalls = Internal::find_direct_calls(blk.function());
    for (Func a : as) {
        if (bcalls.count(a.name())) {
            Func ai = a.in(blk);
            // rows innermost: the block's extent is a guarded min(), not 1
            ai.reorder_storage(ai.args()[1], ai.args()[0]).compute_at(at).vectorize(ai.args()[1]);
        }
    }
    Stage au = acc.update();
    grouped ? au.split(m, mo, mi, group).reorder(n, bacc, mi, mo, ryo) : au.reorder(n, bacc, m, ryo);
    regs(acc.compute_at(tile_at(out, st)), {n}, {bacc, m, mi, mo});
}

// The integer path on i8mm smmla (2 x 2 x 8 matrix-multiply tiles): nt x mt
// tiles of 2 x 2 sub-tiles, each 4 dense lanes of the intermediates' storage
// (split_storage), so the tile's accumulators stay in registers.
inline void blocked_mmla(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r,
                         const std::vector<ApproximationResult> &rs, bool derived = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], nio("nio"), nii("nii"), mio("mio"), mii("mii"), u("u"), bacc("bacc");
    RVar ry = r.y, rxc("rxc"), rxk("rxk"), ryo("ryo"), ryi("ryi");
    bool aff = affine(rs);
    Sums d;
    if (derived && derived_sums(out, st, dot, s, cond, r, rs, d)) aff = false;
    s.split(r.x, rxc, rxk, 8);
    Func blk = s.rfactor(ry, u);
    Func rest, mid, codes = block_dot(blk, rs, true, rest, mid, aff);
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor(ryi, bacc);
    // 2 x 2 sub-tiles (loops `order`, innermost first), guarded: this code is
    // also in other tiles' branches (never run there), whose bounds must not
    // grow past their tiles
    auto sub = [&](Stage st, const std::vector<VarOrRVar> &order, const std::vector<VarOrRVar> &unrolled) {
        st.split(n, nio, nii, 2, TailStrategy::GuardWithIf).split(m, mio, mii, 2, TailStrategy::GuardWithIf);
        st.reorder(order).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio);
        for (const VarOrRVar &v : unrolled) {
            st.unroll(v);
        }
        return st;
    };
    s.vectorize(n, 2, TailStrategy::GuardWithIf).unroll(n).unroll(m);
    for (Func f : {acc, blk, codes, mid}) {
        if (!f.defined()) continue;
        Var x = f.args()[2], sno, sni, smo, smi;  // x: bacc or u
        f.split_storage(n, sno, sni, 2).split_storage(m, smo, smi, 2).reorder_storage(sni, smi, sno, smo, x);
        sub(f, {nii, mii, nio, mio, x}, {x});
        // per block (per 2 rows, tall tiles keep the weight's codes live and spill)
        if (x.name() == u.name()) f.compute_at(acc, ryo).bound_storage(u, ts.interleave);
    }
    acc.compute_at(tile_at(out, st));
    sub(acc.update(), {nii, mii, nio, mio, bacc, ryo}, {bacc});
    for (Func f : {blk, mid}) {
        if (f.defined()) sub(f.update(), {nii, mii, nio, mio, u}, {u});
    }
    sub(codes.update(), {rxk, nii, mii, rxc, nio, mio, u}, {rxc, u}).atomic().vectorize(rxk);
    if (rest.defined()) {  // the act's block sum, once per act column (n vectorized)
        regs(rest.compute_at(acc, ryo).bound_storage(u, ts.interleave), {n}, {m, u});
        rest.update().reorder(rxk, rxc, n, m, u).atomic().vectorize(rxk).vectorize(rxc);
    }
    schedule_sums(d, out, st, acc, n, m, ts, r, true, 16);
    tables(rs, {blk, mid, codes, rest}, LoopLevel(acc, ryo));
}

// An encoder computed per record (each block of values), vectorized: the
// Funcs with an element per value (e.g. the codes, packed bytes) across their
// elements, and its reductions (e.g. the block's scale) across the block.
inline void encoder(Func e, const std::vector<ApproximationResult> &rs) {
    std::vector<Func> dec = decoders(rs), tab = decoders(rs, Of::Tables);
    dec.insert(dec.end(), tab.begin(), tab.end());
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

// The N = M = 1 schedule (GGML's vec_dot). Activations encoded inside the
// pipeline (`staged`) are encoded once, up front.
inline void vec_dot(Func out, const std::vector<Func> &dots, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t) {
    for (Func e : staged) {
        e.compute_root();
        encoder(e, rs);
        if (blocks[1] == 1) e.vectorize(e.args()[0], t.natural_vector_size<float>() * 4, TailStrategy::GuardWithIf);
    }
    out.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);  // N = M = 1
    if (blocks[0] == 1) {
        flat(out, 0, dots[0], Expr(), {1, 1, 4}, r, t);
        return;
    }
    blocked(out, 0, dots[0], Expr(), {}, r, blocks[1] == blocks[0], rs, t, true);
}

// Metal (Apple GPUs), as GGML's kernel_mul_mv_q4_0_f32: threadgroups of `sg`
// simdgroups (gpu_threads), each `nr` rows of one column of out. A lane takes
// half a block (one of its two 8-byte halves: 16 values) of every 16th block,
// accumulating the rows in registers (the codes' sum per block, then times
// its scale; an affine weight's min times the act's sum alongside, as
// block_dot); the lanes' sums then reduce across the simdgroup (an atomic
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
    Func blk = s.rfactor({{rxh, uh}, {ry, u}}), rest, mid;
    Func codes = block_dot(blk, rs, false, rest, mid, affine(rs));
    s.split(ry, ryo, ryi, 16, TailStrategy::GuardWithIf);
    Func acc = s.rfactor({{rxh, uh}, {ryi, ub}});
    s.fuse(rxh, ryi, rl).atomic().gpu_lanes(rl);
    acc.reorder_storage(uh, ub, n, m).compute_at(out, ns);
    acc.fuse(uh, ub, l).reorder(n, m, l).gpu_lanes(l).unroll(n);
    acc.update().fuse(uh, ub, l).reorder(n, m, ryo, l).gpu_lanes(l).unroll(n);
    // store_at: works around halide/Halide#9571; bound_storage: also in the K tail: registers
    for (Func f : {blk, mid, codes, rest}) {
        if (f.defined()) f.compute_at(acc, ryo).store_at(acc, l).bound_storage(u, 1).unroll(f.args()[0]);
    }
    blk.update().unroll(blk.args()[0]);
    for (Func f : {codes, rest}) {
        if (f.defined()) f.update().reorder(f.args()[0], rxi, rxn).unroll(f.args()[0]).unroll(rxi).unroll(rxn);
    }
}

// Rowwise formats (schemes/kleidiai.h: weights qsi8cx, acts qai8dx): one
// scale per row, so the codes' dot accumulates in int32 over the whole of K.
// dot's increment, the weight's codes times its scale times the act's codes
// plus its offset times its scale (s, out's stage tile under `cond`), splits
// on the act's codes (an integer call reading m and r): one Func the codes'
// dot (int32), the other the weight's code sum, read from its derived port
// (Func::approximate_by(DerivedPort, ...): the row sums its encoder stored),
// each times its scales once per output. Returns the codes' dot.
inline Func rowwise_dot(Stage s, Func dot, const Expr &cond, const std::vector<ApproximationResult> &rs) {
    std::string m = dot.args()[1].name();
    CallTest act_codes = [m](const Internal::Call *c) {
        if ((c->call_type != Internal::Call::Halide && c->call_type != Internal::Call::Image) || !c->type.is_int()) return false;
        bool row = false, reduced = false;
        for (const Expr &a : c->args) {
            Internal::visit_with(a, [&](auto *, const Internal::Variable *v) {
                row |= v->name == m;
                reduced |= v->reduction_domain.defined();
            });
        }
        return row && reduced;
    };
    s.eager_inline(decoders(rs, Of::Floats));
    auto [with, sums] = split_on(s, update_value(dot, cond), dot.name(), act_codes);
    if (!with.defined() || !sums.defined()) throw std::invalid_argument("rowwise_dot: no zero-point term in " + dot.name());
    Func codes = with.change_type(Int(32)), sum = sums.change_type(Int(32));
    for (const DerivedPort &p : rs[0].derived_ports()) {
        if (match_derived_port(sum, p)) {
            sum.approximate_by(p, {sums});
            return codes;
        }
    }
    throw std::invalid_argument("rowwise_dot: the weight's code sum matches none of its derived ports");
}

// The rowwise gemv: per tile of ts.nt rows, the codes' dot in int32 lanes
// over all of K (sdot: 4 products per lane), merged once at the end. Weight
// records of rows interleaved in `chunk`-code pieces: the lanes across the
// record's rows and each piece's 4-code quads, so a piece of the record is
// one dense load and the act's piece is broadcast to its rows (KleidiAI's
// qsi8cxp4x8 gemv); planar weights (chunk 0): each row's 16-code lanes,
// the act's shared by the rows.
inline void rowwise_gemv(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int chunk,
                         const std::vector<ApproximationResult> &rs) {
    Stage s = tile_out(out, st, dot, cond, ts, r, chunk > 0);
    Func codes = rowwise_dot(s, dot, cond, rs);
    Var n = dot.args()[0], m = dot.args()[1], q("q");
    if (ts.nt > 1) s.vectorize(n);  // the scaling, once per output
    RVar rx = r.x, ry = r.y, rp("rp"), rq("rq"), ri("ri"), ro("ro");
    LoopLevel tile = tile_at(out, st);
    Stage cu = codes.update();
    if (chunk) {
        cu.split(rx, rp, rx, chunk).split(rx, rq, ri, 4);
        Func quads = cu.rfactor(rq, q);
        cu.reorder(rq, n).atomic().vectorize(rq);
        quads.update().reorder(ri, q, n, rp, ry, m).atomic().vectorize(ri);
        regs(quads.reorder_storage(q, n, m).compute_at(tile), {q, n}, {m, rp});
    } else {
        cu.split(rx, ro, ri, 4);
        Func lanes = cu.rfactor(ro, q);
        cu.reorder(ro, n).atomic().vectorize(ro);
        lanes.update().reorder(ri, q, n, ry, m).atomic().vectorize(ri);
        regs(lanes.reorder_storage(q, n, m).compute_at(tile), {q}, {n, m});
    }
    regs(codes.compute_at(tile), {}, {n, m});
}

// The rowwise gemm on i8mm smmla (2 x 2 x 8 matrix-multiply tiles), as
// blocked_mmla but with no per-block work: nt x mt tiles of 2 x 2 sub-tiles,
// each 4 dense lanes of the codes' dot (split_storage), in registers over
// all of K.
inline void rowwise_mmla(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, bool whole_n,
                         const std::vector<ApproximationResult> &rs, bool guard_m = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r, whole_n, guard_m);
    Func codes = rowwise_dot(s, dot, cond, rs);
    s.vectorize(dot.args()[0]);  // the scaling, once per output
    Var n = dot.args()[0], m = dot.args()[1], nio("nio"), nii("nii"), mio("mio"), mii("mii"), sno, sni, smo, smi;
    RVar rx = r.x, ry = r.y, rxc("rxc"), rxk("rxk");
    auto sub = [&](Stage st, const std::vector<VarOrRVar> &order) {
        st.split(n, nio, nii, 2, TailStrategy::GuardWithIf).split(m, mio, mii, 2, TailStrategy::GuardWithIf);
        return st.reorder(order).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio);
    };
    codes.split_storage(n, sno, sni, 2).split_storage(m, smo, smi, 2).reorder_storage(sni, smi, sno, smo);
    sub(codes, {nii, mii, nio, mio});
    codes.compute_at(tile_at(out, st));
    if (guard_m) {
        // (a constant under cond; M = 1, the gemv, needs only its row)
        codes.bound_extent(m, select(out.output_buffer().dim(1).extent() > 1, ts.mt, 1));
    }
    Stage cu = codes.update();
    cu.split(rx, rxc, rxk, 8);
    sub(cu, {rxk, nii, mii, nio, mio, rxc, ry}).unroll(rxc).atomic().vectorize(rxk);
}

// A rowwise act encoded in the pipeline (qai8dx's codes e): its row ranges
// (min and max, vectorized), then the row's scale and zero point, computed
// once per row (so the products' scaling reads them too), then the codes
// (vectorized), each in parallel over the rows when there are several. With
// `pairs`, the codes are stored in groups of 8 rows (M > 1 rounded up: the
// rows past M repeat the last one), in 8-code pieces, the group's pieces adjacent
// and its K contiguous (split_storage, as KleidiAI's qai8dxp packs them for
// smmla): an smmla operand (8 codes of 2 rows) is one dense load, and a tile
// of 8 rows from 0 mod 8 one stream. One row (gemv) is dense: groups of 1,
// computed within out's specializations, whose M > 1 or M = 1 folds the
// layout (no stride left to the loops).
inline void rowwise_encoder(Func e, Func out, const std::vector<ApproximationResult> &rs, const Expr &M, bool pairs) {
    std::vector<Func> dec = decoders(rs);
    for (const ApproximationResult &res : rs) {
        if (res.encoded[0].name() != e.name()) continue;
        for (Func f : res.intermediates) {
            if (std::any_of(dec.begin(), dec.end(), [&](const Func &d) { return d.name() == f.name(); })) continue;
            Var row = f.args().back();
            if (f.name() == e.name()) {
                // (specializations copy the schedule so far: vectorize first)
                Var j = f.args()[0], b = f.args()[1], sjo, sji, sro, sri, ro("ro"), ri("ri");
                Stage s = f.compute_at(out, Var::outermost()).vectorize(j);
                if (pairs) {
                    f.bound(row, 0, select(M > 1, (M + 7) / 8 * 8, M)).split_storage(j, sjo, sji, 8).split_storage(row, sro, sri, select(M > 1, 8, 1)).reorder_storage(sji, sri, sjo, b, sro);
                    // 8 rows per task: a cache line's pieces in one task
                    s.specialize(M > 1).split(row, ro, ri, 8, TailStrategy::GuardWithIf).parallel(ro);
                } else {
                    s.specialize(M > 1).parallel(row);
                }
            } else if (f.has_update_definition()) {
                // per lane (a value's place in its block) over the blocks, one
                // vector of each, then across the lanes once per row
                Var u("u");
                RVar rx = f.rvars()[0];
                Func lanes = f.update().rfactor(rx, u);
                lanes.compute_at(f, row).vectorize(u).update().vectorize(u);
                Stage(f.compute_root()).specialize(M > 1).parallel(row);
                f.update().specialize(M > 1).parallel(row);
            } else if (f.dimensions() == 1) {
                f.compute_root();
            }
        }
    }
}

// mul_mat on rowwise formats: gemm (M > 1, N > 1) on smmla tiles where the
// target has i8mm, 8 x 8 (16 accumulators: enough independent smmla chains to
// fill the pipes, as KleidiAI's 16 x 4), else 4 x 8; on acts encoded here,
// tiles of whole groups of 8 rows (the last guarded), any M > 1; on
// pre-quantized acts, shifted inwards (4 x 2 for M < 8); gemv
// tiles of 4 rows (a record of interleaved weights; planar: N >= 4, else
// single rows), in tasks of 16 rows.
inline void rowwise_mul_mat(Func out, const std::vector<Func> &dots, const RDom &r, const std::vector<int> &blocks,
                            const std::vector<ApproximationResult> &rs, const std::vector<Func> &staged, const Target &t) {
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    if (dots.size() > 1 || blocks[2] > 1) throw std::invalid_argument("rowwise: act records of one row");
    int chunk = blocks[3] > 1 ? blocks[4] : 0;
    for (Func e : staged) {
        rowwise_encoder(e, out, rs, M, t.has_feature(Target::ARMI8MM));
    }
    Func dot = dots[0];
    Tiles mmla8x8{8, 8, 1, 32, 32, 2}, mmla4x8{4, 8, 1, 32, 32, 2}, mmla4x2{4, 2, 1, 32, 32, 2}, rows{4, 1, 1, 16}, single{1, 1, 1, 16};
    auto fits = [&](const Tiles &ts) { return N >= ts.nt && M >= ts.mt; };
    if (t.has_feature(Target::ARMI8MM)) {
        // records of 4 rows: whole 8-row tiles only where N allows
        if (!staged.empty()) {
            // acts encoded here, in groups of 8 rows (padded): whole groups
            rowwise_mmla(out, 0, dot, N >= 8 && M > 1 && (chunk == 0 || N % 8 == 0), mmla8x8, r, chunk > 0, rs, true);
            // (records: N >= 4, so the gemv after learns M = 1)
            rowwise_mmla(out, 0, dot, chunk ? M > 1 : N >= 4 && M > 1, mmla4x8, r, chunk > 0, rs, true);
        } else {
            rowwise_mmla(out, 0, dot, fits(mmla8x8) && (chunk == 0 || N % 8 == 0), mmla8x8, r, chunk > 0, rs);
            rowwise_mmla(out, 0, dot, fits(mmla4x8), mmla4x8, r, chunk > 0, rs);
            rowwise_mmla(out, 0, dot, fits(mmla4x2), mmla4x2, r, chunk > 0, rs);
        }
    }
    if (chunk) {  // N is a multiple of the records' rows
        rowwise_gemv(out, 0, dot, Expr(), rows, r, chunk, rs);
        return;
    }
    // (after the smmla tiles on encoded acts, M = 1: said, so that the acts'
    // layout folds to dense rows)
    bool dense = t.has_feature(Target::ARMI8MM) && !staged.empty();
    rowwise_gemv(out, 0, dot, dense ? N >= rows.nt && M == 1 : fits(rows), rows, r, 0, rs);
    rowwise_gemv(out, 0, dot, Expr(), single, r, 0, rs);
}

// mul_mat: gemm tiles the outputs (4 x 8 on smmla when the target has i8mm
// and both operands are quantized alike; for whole act records, 4 x 12, 16 or
// 20, whichever pads M least, 16 x 4 for one record, 4 x 8 for two; on weight
// records in 4-code pieces, those tiles on by-element sdot; else 4 x 4 on
// sdot), in tasks of up to 32 x 32 outputs (whole records: up to 128 rows),
// fewer rows where that balances the threads (small N);
// gemv (M = 1) is the vec_dot recipe on row pairs sharing each activation
// block (single rows for N = 1, or for weights decoded through tables). Edge
// tiles shift inwards: any N, M at least the tile's. With act records of
// r = blocks[2] > 1 rows, out's first update is the rows in whole records
// (dots[1]) on gemm tiles starting on a record, its second the others
// (dots[0], < r rows) on gemv tiles.
inline void mul_mat(Func out, const std::vector<Func> &dots, const RDom &r, const std::vector<int> &blocks, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t) {
    if (t.has_feature(Target::Metal)) {
        if (!staged.empty() || blocks[2] > 1 || blocks[3] > 1) throw std::invalid_argument("metal: act formats stored as computed, weights one row per record");
        metal_rows(out, dots[0], r, rs);
        return;
    }
    if (blocks.size() > 5 && blocks[5]) {
        rowwise_mul_mat(out, dots, r, blocks, rs, staged, t);
        return;
    }
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    bool integer = blocks[1] == blocks[0], whole = blocks[2] > 1;
    Expr Mr = M / blocks[2] * blocks[2];
    const int fv = 4 * t.natural_vector_size<float>();
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
        if (blocks[1] == 1) {  // values: vectorized (also for M = 1)
            s.vectorize(b, fv, TailStrategy::GuardWithIf);
            e.vectorize(b, fv, TailStrategy::GuardWithIf);
        }
    }
    if (blocks[0] == 1) {
        // Float weights (no blocks): 4 x 2 tiles (each vector of a row's
        // weights used for both act columns, each act vector for 4 rows), row
        // pairs, single rows. gemv (M = 1) takes single rows: one weight
        // stream (row pairs are two, a row apart: 0.65x GGML at K = 14336).
        if (whole || blocks[3] > 1) throw std::invalid_argument("weights without blocks: one row per record, acts too");
        Tiles fg{4, 2, 1, 32, 32, 2}, fp{2, 1, 2, 16}, fr{1, 1, 4, 16};
        flat(out, 0, dots[0], N >= fg.nt && M >= fg.mt, fg, r, t);
        flat(out, 0, dots[0], N >= fp.nt && M > 1, fp, r, t);
        flat(out, 0, dots[0], Expr(), fr, r, t);
        return;
    }
    Tiles mmla8{4, 8, 1, 32, 32, 2}, gemm{4, 4, 1, 32, 32, 2}, pairs{2, 1, 2, 16}, rows{1, 1, 2, 16};
    bool mmla_ok = integer && t.has_feature(Target::ARMI8MM);
    // Weight records in 4-code pieces: by-element sdot on the same tiles (on M3,
    // KleidiAI's dotprod gemm beats both i8mm ones)
    bool lanes = whole && integer && blocks[3] > 1 && blocks[4] == 4, tiled = mmla_ok || lanes;
    // Whole records: the tiles' rows past Mr read clamped records, so one
    // row tile (dot's bound) covers every record, with mmla when there is.
    Func dr = dots.back();
    auto fits = [&](const Tiles &ts, Expr m) { return whole ? N % ts.nt == 0 && N >= ts.nt : N >= ts.nt && m >= ts.mt; };
    // Whole records' row tiles: 12, 16 or 20 rows cost about the same per row,
    // so the one computing the fewest rows past Mr (ties: 16, then 20); one
    // record, 16 x 4 (16 accumulators, no rows past Mr); two, 8 rows. Each
    // task's tiles cover up to 128 rows, so the weights stream once (M <= 128).
    // A chain, so the later branches learn each condition false (dr's bound).
    Tiles t16{4, 16, 1, 32, 128, 2}, t20{4, 20, 1, 32, 120, 2}, t12{4, 12, 1, 32, 120, 2}, t8{4, 8, 1, 32, 128, 2}, wide{16, 4, 1, 32, 128, 2};
    auto padded = [&](const Tiles &ts) { return (Mr + ts.mt - 1) / ts.mt * ts.mt; };
    Expr fewest = min(padded(t12), padded(t16), padded(t20)), odd_n = !fits(gemm, Mr);
    std::vector<std::pair<Expr, Tiles>> tiles{{Mr == wide.mt && fits(wide, Mr), wide}, {Mr <= t8.mt, t8}, {padded(t16) == fewest, t16}, {padded(t20) == fewest, t20}, {Expr(), t12}};
    if (whole) {  // odd N first, as tall as t16; the bound is then constant in each branch
        blocked(out, 1, dr, odd_n, {1, tiled ? t16.mt : gemm.mt, 1, 32, 32, 2}, r, integer, rs, t);
        Expr mt = t12.mt;
        for (size_t i = tiles.size() - 1; i-- > 0;) {
            mt = select(tiles[i].first, tiles[i].second.mt, mt);
        }
        dr.bound_extent(dr.args()[1], tiled ? select(odd_n, t16.mt, mt) : Expr(gemm.mt));
    }
    if (whole && tiled) {
        for (auto [c, ts] : tiles) {
            lanes ? blocked_rows(out, whole, dr, c, ts, r, blocks[4], rs, blocks[2]) : blocked_mmla(out, whole, dr, c, ts, r, rs);
        }
    } else if (mmla_ok) {
        blocked_mmla(out, whole, dr, fits(mmla8, M), mmla8, r, rs, true);
    }
    if (!whole || !tiled) blocked(out, whole, dr, whole ? Expr() : fits(gemm, M), gemm, r, integer, rs, t, !whole);
    // The rows past Mr (fewer than a record's): no tasks when there are none.
    Expr rest = whole ? M % blocks[2] != 0 : Expr();
    if (integer && blocks[3] > 1) {  // N is a multiple of the records' rows
        blocked_rows(out, 2 * whole, dots[0], Expr(), {blocks[3], 1, 2, 16, 0, 0, rest}, r, blocks[4], rs);
        return;
    }
    pairs.busy = rows.busy = rest;
    // A table's decode, per row pair, outweighs sharing the act's loads
    if (decoders(rs, Of::Tables).empty()) blocked(out, 2 * whole, dots[0], fits(pairs, 1), pairs, r, integer, rs, t, true);
    blocked(out, 2 * whole, dots[0], Expr(), rows, r, integer, rs, t, true);
}

}  // namespace ggml
