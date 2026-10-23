#pragma once

// Schedules of the mul_mat algorithm (kernels/matmul.cpp). They see its
// structure only: the reduction r (r.x within a block, r.y over the blocks),
// the values per record of each operand, and the operands'
// ApproximationResults.
#include "Halide.h"
#include "schemes/tags.h"

#include <functional>
#include <set>

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

// Which components' decode to pick, by their port tags (schemes/tags.h):
// only(t) those tagged any of t, all_but(t) the rest (including the Funcs
// of several components, or of none: the values decoded from them).
struct Pick {
    ApproximationTags tags;
    bool but;
    bool operator()(const ApproximationTags &t) const {
        bool any = std::any_of(t.begin(), t.end(), [&](const std::string &s) { return tags.count(s); });
        bool all = std::all_of(t.begin(), t.end(), [&](const std::string &s) { return tags.count(s); });
        return but ? t.empty() || !all : any;
    }
};
inline Pick only(ApproximationTags t) {
    return {std::move(t), false};
}
inline Pick all_but(ApproximationTags t) {
    return {std::move(t), true};
}

// The decode Funcs eager_inline can fold into a consumer, of rs[first, last),
// of the components `pick` (e.g. the codes' decode, or all but the codes':
// the scales, offsets and the values decoded from them); with `tables`, the
// ones reading a table instead, which a vectorized inline would gather from:
// see tables().
inline std::vector<Func> decoders(const std::vector<ApproximationResult> &rs, const Pick &pick = all_but({}), size_t first = 0, size_t last = SIZE_MAX, bool tables = false) {
    std::vector<Func> fs;
    for (size_t i = first; i < std::min(last, rs.size()); i++) {
        for (Func f : rs[i].decode_funcs()) {
            if (pick(rs[i].tags_of(f)) && tables == table_of(f).defined()) fs.push_back(f);
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

// The operands' formats, as the schedules see them (matmul.cpp's): the
// weight's values per block (1: floats, no blocks), rows per record
// (interleaved in pieces of `chunk` codes) and whether it is rowwise; the act
// codes' values per block (the weight's: an integer dot; 1: values) and rows
// per record; two-level formats' values per sub-block (0: one level), the
// weight's each with its own scale, the act's each with its own derived sum.
struct Formats {
    int block = 1, rows = 1, chunk = 0, act_block = 1, act_rows = 1;
    bool rowwise = false;
    int sub = 0, act_sub = 0;
};

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
// Tiles under their conditions, in order (the last undefined: the rest),
// on one stage of out: the later branches learn each earlier false.
using Chain = std::vector<std::pair<Expr, Tiles>>;

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

// Does e call any of fs?
inline bool calls_any(const Expr &e, const std::vector<Func> &fs) {
    bool found = false;
    Internal::visit_with(e, [&](auto *self, const Internal::Call *c) {
        found |= std::any_of(fs.begin(), fs.end(), [&](const Func &f) { return c->name == f.name(); });
        self->visit_base(c);
    });
    return found;
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
    for (Func f : decoders(rs, all_but({}), 0, SIZE_MAX, true)) {
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

// Then a term of the rest (e.g. its block sum) reads the first of `ports`
// whose definition it matches (Func::approximate_by(DerivedPort, ...)) in
// `consumer`: false if none does. A port whose type does not widen exactly
// to the term's (e.g. q8_K's int16 bsums, of an int32 sum) replaces the term
// retyped to it (change_type proves the narrower sum exact), which the term
// then casts.
inline bool read_port(Func term, const std::vector<DerivedPort> &ports, Func consumer) {
    for (const DerivedPort &p : ports) {
        if (match_derived_port(term, p)) {
            Type st = p.stored.type(), tt = term.type();
            if (st == tt || (st.is_float() && tt.is_float() && st.bits() <= tt.bits())) {
                term.approximate_by(p, {consumer});
            } else {
                term.change_type(st).approximate_by(p, {term});
            }
            return true;
        }
    }
    return false;
}

// Does the weight's decode add a term free of its codes (q4_1's min: a port
// tagged offset)?
inline bool affine(const std::vector<ApproximationResult> &rs) {
    const ApproximationPorts &ps = rs[0].encoded_ports;
    return std::any_of(ps.begin(), ps.end(), [](const ApproximationPort &p) { return p.tags && p.tags->count(tag::offset); });
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

// Has the weight a float scale per sub-block (a decoded scale port with
// the values' dimensions: (sub-block, block, row))?
inline bool float_sub_scales(const std::vector<ApproximationResult> &rs) {
    for (Func f : decoders(rs, only({tag::scale}), 0, 1)) {
        if (f.type().is_float() && f.dimensions() == 3) return true;
    }
    return false;
}

// The weight's decoded per-sub-block scales (and mins): its scale decoders
// with the values' dimensions that no other scale decoder reads.
inline std::vector<Func> scale_decoders(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> sc = decoders(rs, only({tag::scale}), 0, 1), fs;
    for (Func f : sc) {
        bool read = std::any_of(sc.begin(), sc.end(), [&](const Func &g) {
            const auto &vals = g.function().values();
            return std::any_of(vals.begin(), vals.end(), [&](const Expr &e) { return calls_any(e, {f}); });
        });
        if (f.dimensions() == 3 && !read) fs.push_back(f);
    }
    return fs;
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
inline Func block_dot(Func blk, const std::vector<ApproximationResult> &rs, bool integer, Func &rest, Func &mid, bool aff, const std::vector<Func> &kept = {}) {
    Func codes;
    rest = mid = Func();
    if (!aff) {
        std::vector<Func> dec;
        for (Func f : decoders(rs)) {
            if (std::none_of(kept.begin(), kept.end(), [&](const Func &k) { return k.name() == f.name(); })) dec.push_back(f);
        }
        blk.update().eager_inline(dec);
        // (scales per sub-block alone, as nvfp4's with f32 acts: none hoists)
        codes = invariant_factor(blk) ? blk.update().hoist_invariants()[0] : blk;
    } else {
        blk.update().eager_inline(decoders(rs, all_but({tag::codes})));
        if (invariant_factor(blk)) mid = blk.update().hoist_invariants()[0];
        FuncVec parts = (mid.defined() ? mid : blk).update().hoist_invariants({decoders(rs, only({tag::codes}), 0, 1)});
        codes = parts[0];
        rest = parts[1];
        for (Func f : {codes, rest}) {
            f.update().eager_inline(decoders(rs, only({tag::codes})));
        }
        if (integer) rest = rest.change_type(Int(32));
    }
    return integer ? codes.change_type(Int(32)) : codes;
}

// An affine weight's code-free terms, read from the act's derived port where
// one is their act part: dot's increment (s, out's stage st tile under
// `cond`) splits on the weight's codes (hoist_invariants) into `with`,
// computed per tile, s then its update, and the rest, d.sums, the weight's
// min times the act; per block (r.y rfactored to u, d.blk), the min hoists
// out of the act's block sum, which then reads the act's derived port
// (read_port: e.g. q8_1's s; d.sum, if none matches). Acts whose derived
// port sums sub-blocks of `act_sub` values (q8_K's bsums): the block's sum,
// int32 (d.sum: the act's codes decoded up to what its ports read), splits
// per act sub-block (rfactor: d.part), the weight's factor of each (its
// sub-block's min) hoists out, and the act's sub-block sum reads the port.
// With `sub` (a multiple of act_sub): the weight's sub-blocks instead, each
// min times the act's sum over it (d.pair), the sum of its act sub-blocks'
// port values (as GGML's repacked q4_K gemv pair-sums q8_K's bsums).
// Returns false (dot untouched) if the weight is not affine or the act has
// no derived port.
struct Sums {
    Func sums, blk, sum, part, pair;
};
inline bool derived_sums(Func out, int st, Stage &s, const RDom &r, const std::vector<ApproximationResult> &rs, Sums &d, int act_sub = 0, int sub = 0, const std::vector<Func> &kept = {}) {
    d = Sums();
    if (rs.size() < 2 || rs[1].derived_ports().empty() || !affine(rs)) return false;
    std::vector<Func> dec;
    for (Func f : decoders(rs, all_but({tag::codes}), 0, 1)) {
        if (std::none_of(kept.begin(), kept.end(), [&](const Func &k) { return k.name() == f.name(); })) dec.push_back(f);
    }
    s.eager_inline(dec);
    FuncVec parts = s.hoist_invariants({decoders(rs, only({tag::codes}), 0, 1)});
    if (!parts[1].defined()) throw std::invalid_argument("derived_sums: no terms free of the codes in " + s.name());
    Func with = parts[0];
    d.sums = parts[1];
    d.blk = d.sums.update().rfactor(r.y, Var("u"));
    // a port of the act's codes alone (no scale): the act's scale hoists too
    if (act_sub) d.blk.update().eager_inline(decoders(rs, all_but({tag::codes}), 1));
    d.sum = d.blk.update().hoist_invariants()[0];
    if (act_sub) {
        // the act's codes decoded up to what its ports read: an int32 sum
        std::set<std::string> read;
        for (const DerivedPort &p : rs[1].derived_ports()) {
            for (const Func &f : p.from) {
                read.insert(f.name());
            }
        }
        std::vector<Func> dec = decoders(rs, only({tag::codes}), 1), above;
        std::copy_if(dec.begin(), dec.end(), std::back_inserter(above), [&](const Func &f) { return !read.count(f.name()); });
        d.sum.update().eager_inline(above);
        d.sum = d.sum.change_type(Int(32));
        RVar rxt("rxt"), rxj("rxj"), rxh("rxh"), rxa("rxa");
        bool pairs = sub > act_sub;
        Stage ds = d.sum.update();
        if (pairs) {
            // r.x = (rxt * sub/act_sub + rxh) * act_sub + rxj, as the port's
            // definition indexes its sub-blocks (matched structurally)
            ds.split(r.x, rxa, rxj, act_sub).split(rxa, rxt, rxh, sub / act_sub);
        } else {
            ds.split(r.x, rxt, rxj, act_sub);
        }
        d.part = ds.rfactor(rxt, Var("t"));
        Func term = d.part.update().hoist_invariants()[0], reader = d.part;
        if (pairs) {
            // the act's codes: two sub-blocks' sum fits int16, as the port
            d.pair = term.change_type(Int(16));
            reader = d.pair;
            term = d.pair.update().rfactor(rxh, Var("h"));
        }
        if (!read_port(term, rs[1].derived_ports(), reader)) {
            throw std::invalid_argument("derived_sums: the act's sub-block sums in " + s.name() + " match none of its derived ports");
        }
    } else if (read_port(d.sum, rs[1].derived_ports(), d.blk)) {
        d.sum = Func();
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
// `across_rows`: the sub-block sums too vectorized across the rows (n;
// records of several rows), each act sub-block's (or d.pair's) sum
// broadcast: by-element multiply-adds. `act_records`: the act too in whole
// records (its sums interleaved across its rows).
inline void schedule_sums(Sums d, Func out, int st, Func acc, Var n, Var m, const Tiles &ts, const RDom &r, bool gemm, int vec, bool across_rows = false, bool act_records = false) {
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
    if (d.part.defined() && across_rows) {
        Var t = d.part.args().back();
        RVar rxt("rxt");
        regs(d.sum.compute_at(blocks), {n}, {u, m});
        d.sum.update().reorder(n, rxt).unroll(rxt);
        regs(d.part.compute_at(blocks).reorder_storage(n, t, u, m), {n}, {t, u, m});
        if (d.pair.defined()) {
            // the act's sums per weight sub-block, one vector (pairwise adds
            // of its port's), each a lane: a by-element multiply-add
            RVar rxh("rxh");
            if (act_records) {
                // whole act records (q8_K.4x8): each weight sub-block's act
                // sums dense across the record's rows (h, tl, m), so pairwise
                // adds of whole bsums vectors (addp), not lane gathers
                Var tg("tg"), tl("tl"), sg("sg"), sl("sl");
                d.pair.compute_at(blocks).split_storage(t, sg, sl, 2).reorder_storage(sl, m, sg, n, u);
                d.pair.split(t, tg, tl, 2).reorder(tl, m, tg, n, u).vectorize(tl).vectorize(m).unroll(tg).unroll(n).unroll(u);
                d.pair.update().split(t, tg, tl, 2).reorder(rxh, tl, m, tg, n, u).atomic().vectorize(rxh).vectorize(tl).vectorize(m).unroll(tg).unroll(n).unroll(u);
            } else {
                regs(d.pair.compute_at(blocks).reorder_storage(t, n, u, m), {t}, {n, u, m});
                d.pair.update().reorder(t, rxh, n).unroll(rxh);
            }
        }
    } else if (d.part.defined()) {
        // per act sub-block, vectorized across them: its factor times its sum
        Var t = d.part.args().back();
        RVar rxt("rxt"), rxj("rxj");
        d.sum.compute_at(blocks).unroll(u).unroll(n).unroll(m);
        d.sum.update().atomic().vectorize(rxt).unroll(u).unroll(n).unroll(m);
        regs(d.part.compute_at(blocks).reorder_storage(t, u, n, m), {t}, {u, n, m});
        if (d.pair.defined()) {
            // the act's sums per weight sub-block (pairwise adds of its
            // port's), vectorized across them as d.part
            RVar rxh("rxh");
            regs(d.pair.compute_at(blocks).reorder_storage(t, u, n, m), {t}, {u, n, m});
            d.pair.update().reorder(t, rxh).unroll(rxh);
        }
    } else if (d.sum.defined()) {  // no port matched: the act's block sums
        d.sum.compute_at(blocks).unroll(u).unroll(n).unroll(m);
        d.sum.update().atomic().vectorize(r.x, vec).unroll(u).unroll(n).unroll(m);
    }
}

// Eagerly inline into f's update every pure Func it calls but `kept` (as
// staged encodes, computed apart), transitively: e.g. a float weight's view
// and decode, so its products read the stored values.
inline void inline_operands(Func f, const std::vector<Func> &kept) {
    for (;;) {
        std::vector<Func> fs;
        std::set<std::string> seen;
        for (const Func &k : kept) {
            seen.insert(k.name());
        }
        for (const Expr &v : f.function().update(0).values()) {
            Internal::visit_with(v, [&](auto *self, const Internal::Call *c) {
                if (c->call_type == Internal::Call::Halide && c->func.defined() && seen.insert(c->name).second) {
                    Internal::Function g(c->func);
                    if (!g.has_update_definition() && !g.has_extern_definition() && g.definition().specializations().empty()) {
                        fs.emplace_back(g);
                    }
                }
                self->visit_base(c);
            });
        }
        if (fs.empty()) return;
        f.update().eager_inline(fs);
    }
}

// s's sum as a tree of partial sums (GGML's accumulators): a chain of
// rfactors, level i keeping the RVars of levels[i] as its Vars, so it sums
// the others of level i - 1's (level 0: s's other RVars, `outer` outside n
// and m; s then sums the last level's). The order is the schedule's; with
// `acc` (an Accumulate), the first `narrow` levels' sums are acc's: each
// approximated in its consumer (the next level), whose sum then reads it as
// stored (level 0 its operands as stored, but `kept`). Each level per tile
// (`at`), in registers: stored its first `fused` Vars, then n, m; vectorized
// across those (fused into one), unrolled across its others, n, m and the
// RVars it sums. Returns the levels' Funcs.
using Level = std::vector<std::pair<RVar, Var>>;
inline std::vector<Func> sum_tree(Stage s, const std::vector<Level> &levels, LoopLevel at, Var n, Var m, RVar outer, size_t fused = 1,
                                  const Approximation &acc = Approximation(), int narrow = 0, const std::vector<Func> &kept = {}) {
    std::vector<Func> fs;
    for (const Level &l : levels) {
        fs.push_back(s.rfactor(l));
    }
    if (narrow) inline_operands(fs[0], kept);
    for (int i = 0; i < narrow; i++) {
        ApproximationResult a = fs[i].approximate_by(acc, {fs[i + 1]});
        fs[i + 1].update().eager_inline({a.replacement});
        fs[i] = a.encoded[0];
    }
    for (size_t i = 0; i < fs.size(); i++) {
        std::vector<Var> vec;
        std::vector<VarOrRVar> unrolled{n, m};
        for (const auto &[rv, v] : levels[i]) {
            vec.size() < fused ? vec.push_back(v) : unrolled.push_back(v);
        }
        for (const auto &[rv, v] : i ? levels[i - 1] : Level()) {
            unrolled.push_back(rv);  // (those it keeps are Vars here)
        }
        Var l = vec[0];
        for (size_t j = 1; j < vec.size(); j++) {
            Var f(l.name() + "_" + std::to_string(j));
            fs[i].fuse(l, vec[j], f).update().fuse(l, vec[j], f);
            l = f;
        }
        vec.insert(vec.end(), {n, m});
        regs(fs[i].compute_at(at).reorder_storage(vec), {l}, unrolled);
    }
    fs[0].update().reorder(n, m, outer);
    return fs;
}

// The weight quantized in blocks: per block (r.y) and output a dot of the
// codes (when the activation is quantized alike, an int32 sdot: 4 products per
// lane; otherwise f32) times the hoisted scales, accumulated per lane in f32
// (GGML's order), on ts tiles of out under `cond`. `derived`: an affine
// weight's code-free terms read the act's derived port (derived_sums()), a
// product per block accumulated alongside. Two-level weights (`sub` values
// per sub-block, each with its own scale): the block's dot splits per
// sub-block (rfactor), whose scale hoists out of its dot of the codes, so
// the codes' products stay those of the codes (sdot) and the scales apply
// once per sub-block; `act_sub`: the act's (derived_sums(); sub-blocks of
// several act sub-blocks, as q4_K's: their sums paired, d.pair). `sublanes`
// (two-level weights, a per-format parameter): the lanes are the sub-blocks
// instead (mod vec / per), as GGML's NEON iq2_xs: per lane its sub-block's
// dot (sdot, then pairwise adds across the sdot's lanes), and the
// sub-blocks' scales, decoded once per block as a vector, apply as one
// vector multiply-add per vec / per sub-blocks. `gpr_scales` (a per-format
// parameter): the sub-blocks' decoded scales (and mins), per block in
// general-purpose registers, 4 bytes a word (VectorizeIn::GPR: GGML's SWAR
// decode of q4_K's 6-bit scales and mins).
inline void blocked(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t, bool derived = false,
                    int sub = 0, int act_sub = 0, bool sublanes = false, bool gpr_scales = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), u("u"), v("v"), bacc("bacc");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    int per = integer ? 4 : 1, vec = t.natural_vector_size<int8_t>();
    if (sub * per <= vec) sub = 0;  // a sub-block per lane step: no sum to hoist its scale from
    bool sl = sub && sublanes;
    // sub-blocks across the lanes: their scales decoded per block, a vector
    // (derived_sums() leaves them too)
    std::vector<Func> kept;
    if (sl) {
        for (Func f : decoders(rs, only({tag::scale}), 0, 1)) {
            if (f.dimensions() == 3) kept.push_back(f);
        }
    }
    bool aff = affine(rs);
    Sums d;
    // scales decoded in GPRs stay their own stage, for the derived sums and
    // the dot
    std::vector<Func> dkept = kept, gpr;
    if (!sl && gpr_scales) {
        gpr = scale_decoders(rs);
        dkept.insert(dkept.end(), gpr.begin(), gpr.end());
    }
    if (derived && derived_sums(out, st, s, r, rs, d, act_sub, sub > act_sub ? sub : 0, dkept)) aff = false;
    if (aff) kept.clear();
    RVar rxl("rxl");  // (sl: the sub-block's lane)
    if (sl) {
        s.split(r.x, rxs, rxc, sub).split(rxs, rxs, rxl, vec / per).split(rxc, rxc, rxo, vec);
    } else {
        sub ? s.split(r.x, rxs, rxc, sub).split(rxc, rxc, rxo, vec) : s.split(r.x, rxc, rxo, vec);
    }
    s.split(rxo, rxo, rxi, per);
    Func blk = s.rfactor({{sl ? rxl : rxo, lane}, {ry, u}});
    blk.bound(lane, 0, vec / per);  // the lane's range, so the block's scales hoist
    // float sub-block scales (nvfp4's): the codes' dot is integer per
    // sub-block only, once they hoist
    bool fsub = sub && integer && float_sub_scales(rs);
    std::vector<Func> bkept = kept;
    bkept.insert(bkept.end(), gpr.begin(), gpr.end());
    Func rest, mid, codes = block_dot(blk, rs, integer && !fsub, rest, mid, aff, bkept);
    // per sub-block (v): its scale times its codes' dot (inner)
    std::vector<std::tuple<Func, Func, Func>> subs;
    for (Func f : {codes, rest}) {
        if (!sub || !f.defined()) continue;
        Func part = f.update().rfactor(rxs, v);
        part.bound(lane, 0, vec / per);
        Func inner = part.update().hoist_invariants()[0];
        subs.emplace_back(f, part, fsub ? inner.change_type(Int(32)) : inner);
    }
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    RVar rl = sl ? rxl : rxo;
    Func acc = sum_tree(s, {{{rl, lane}, {ryi, bacc}}, {{rl, lane}}}, tile_at(out, st), n, m, ryo)[0];
    s.atomic().vectorize(rl).unroll(n).unroll(m);
    // rfactor put the lanes last in storage; per block group (ryo), the
    // blocks' storage also in the K tail: on the stack
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
        if (f.defined() && !sub) f.update().atomic().vectorize(rxi).unroll(rxc);
    }
    // per block and sub-block, the tile's sub-block dots (inner), then their
    // scales (part)
    std::vector<Func> readers{blk, mid, codes, rest};
    for (auto [f, part, inner] : subs) {
        readers.insert(readers.end(), {part, inner});
        f.update().reorder(lane, n, m, rxs, u).unroll(rxs);
        for (Func g : {part, inner}) {
            regs(g.compute_at(f, rxs).reorder_storage(lane, n, m), {lane}, {v, u, n, m});
        }
        if (sl) {
            // the sub-blocks across the lanes: per lane, its sub-block's
            // sdots accumulated across its chunks (quads, a vector per lane),
            // then summed across their lanes (pairwise adds)
            Var q("q"), ql("ql");
            Func quads = inner.update().rfactor(rxo, q);
            regs(quads.compute_at(f, rxs).reorder_storage(q, lane, n, m), {}, {v, u, n, m});
            quads.fuse(q, lane, ql).vectorize(ql);
            quads.update().reorder(rxi, q, lane).fuse(q, lane, ql).atomic().vectorize(rxi).vectorize(ql).unroll(rxc);
            readers.push_back(quads);
            inner.update().reorder(rxo, lane).atomic().vectorize(rxo).vectorize(lane);
        } else {
            inner.update().atomic().vectorize(rxi).unroll(rxc);
        }
    }
    schedule_sums(d, out, st, acc, n, m, ts, r, ts.nt > 2, vec);
    // ... decoded per block for the derived sums and the sub-blocks' dots,
    // a word of 4 bytes at a time in a GPR (each word's bytes one shift and
    // mask, as GGML's q4_K: its dot from 76 SIMD instructions per block to
    // 72), the mins handed to the derived sums' vectors in registers (fmov,
    // mov.s), the scales extracted from their words
    std::vector<Func> sum_readers;
    if (d.part.defined()) sum_readers.push_back(d.part);
    for (auto [f, part, inner] : subs) {
        sum_readers.push_back(part);
    }
    for (Func k : gpr) {
        std::vector<Func> callers;
        for (Func g : sum_readers) {
            const auto &vals = g.function().update(0).values();
            if (std::any_of(vals.begin(), vals.end(), [&](const Expr &e) { return calls_any(e, {k}); })) callers.push_back(g);
        }
        if (callers.empty()) continue;
        Func w = k.in(callers);
        Var kw("kw"), kb("kb");
        // (all unrolled: a loop here splits the block's code, and LLVM
        // hoists the block's loads above it and spills them)
        w.compute_at(acc, ryo).split(w.args()[0], kw, kb, 4).unroll(kw).vectorize(kb, 4, VectorizeIn::GPR);
        for (size_t i = 1; i < w.args().size(); i++)
            w.unroll(w.args()[i]);
        readers.push_back(w);
    }
    for (auto [f, part, inner] : subs) {
        // the kept scales part reads (the rest inline into them): per block
        // group, vectorized across the sub-blocks
        for (const Expr &e : part.function().update(0).values()) {
            Internal::visit_with(e, [&](auto *self, const Internal::Call *c) {
                for (Func k : kept) {
                    if (c->name == k.name()) {
                        k.compute_at(acc, ryo).vectorize(k.args()[0], vec);
                        readers.push_back(k);
                    }
                }
                self->visit_base(c);
            });
        }
    }
    tables(rs, readers, LoopLevel(acc, ryo));
}

// Weights of one value per record (floats: no blocks, r.x is a single value):
// per output, the products accumulated in f32 in vectors of `vec` values of
// r.y (2 f32 vectors: one of 16-bit floats), ts.interleave independent ones
// (the K tail guarded), then summed across them, on ts tiles of out under
// `cond`. The decoders (conversions to f32) are inlined.
inline void flat(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, const Target &t) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), bacc("bacc");
    RVar ry = r.y, ryo("ryo"), ryi("ryi"), ryl("ryl");
    int vec = 2 * t.natural_vector_size<float>();
    s.split(ry, ryo, ryi, vec * ts.interleave, TailStrategy::GuardWithIf).split(ryi, ryi, ryl, vec);
    sum_tree(s, {{{ryl, lane}, {ryi, bacc}}, {{ryl, lane}}}, tile_at(out, st), n, m, ryo);
    s.atomic().vectorize(ryl).unroll(n).unroll(m);
}

// flat()'s dot accumulated by `acc` (an Accumulate, the format's) in the
// order and roundings of GGML's NEON f16 vec_dot (ggml_vec_dot_f16): with
// lane l = b0 + 2 b1 + 4 b2 + 8 b3 + 16 b4 of each 32 values of r.y, the
// products summed per lane over the 32-value steps (GGML's four 8-lane
// accumulators, fused multiply-adds), then over bit 4 of l (sum[j] +
// sum[j + 2]) and bit 3 (sum[0] + sum[1]), these three by `acc`; then in
// f32 over bit 2 (the halves' add) and bits 0 and 1 (vaddvq's pairwise
// adds): a sum_tree, its first three levels approximated by acc. The K tail
// (K % 32) is guarded into the last step's lanes (GGML sums it apart, in
// f64: not matched).
inline void flat_tree(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, const Approximation &acc, const std::vector<Func> &staged) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], v0("v0"), v1("v1"), v2("v2"), v3("v3"), v4("v4");
    RVar ry = r.y, ryo("ryo"), ryi("ryi"), r16("r16"), r8("r8"), r4("r4"), b0("b0"), b1("b1"), b2("b2"), b3("b3"), b4("b4");
    s.split(ry, ryo, ryi, 32, TailStrategy::GuardWithIf).split(ryi, b4, r16, 16).split(r16, b3, r8, 8).split(r8, b2, r4, 4).split(r4, b1, b0, 2);
    // each level's sum: bit 4, 3, 2, then 0 (bit 1 last, in s)
    Level all{{b0, v0}, {b1, v1}, {b2, v2}, {b3, v3}, {b4, v4}};
    std::vector<Level> levels{all, Level(all.begin(), all.end() - 1), Level(all.begin(), all.end() - 2), Level(all.begin(), all.end() - 3), {all[1]}};
    sum_tree(s, levels, tile_at(out, st), n, m, ryo, all.size(), acc, 3, staged);
    s.unroll(n).unroll(m);
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
    // The act's codes (the decode of rs[1..]'s codes; rs[0] is the weight's)
    // stay out of the inline: staged per block in registers, below; on gemm
    // tiles, so do its scales.
    bool grouped = group && ts.mt > group;
    std::vector<Func> ac = decoders(rs, only({tag::codes}), 1), as = grouped ? decoders(rs, only({tag::scale}), 1) : std::vector<Func>{};
    std::vector<Func> dec = decoders(rs, all_but({}), 0, 1), rest = decoders(rs, grouped ? all_but({tag::codes, tag::scale}) : all_but({tag::codes}), 1);
    dec.insert(dec.end(), rest.begin(), rest.end());
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

// blocked_rows() for affine two-level weights (q4_K's repacked block_q4_Kx8,
// as GGML's ggml_gemv_q4_K_8x8_q8_K): per block and sub-block of `sub`
// values, the int32 dot of the codes vectorized across the record's rows and
// each piece's quads (sdot, the act's piece broadcast to the rows), the
// quads merged, times the sub-block's scales (a vector across the rows),
// summed over the sub-blocks in int32, then times the rows' and act's
// scales into f32 accumulators across the rows. The min term reads the
// act's sub-block sums (`act_sub`: q8_K's bsums; derived_sums()), times
// the rows' mins as a vector, alongside.
inline void blocked_rows_sub(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int chunk,
                             const std::vector<ApproximationResult> &rs, int sub, int act_sub) {
    Stage s = tile_out(out, st, dot, cond, ts, r, true);
    Var n = dot.args()[0], m = dot.args()[1], q("q"), u("u"), v("v"), bacc("bacc");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rp("rp"), rq("rq"), ri("ri"), ryo("ryo"), ryi("ryi_rows");
    Sums d;
    if (!sub || !derived_sums(out, st, s, r, rs, d, act_sub, sub)) throw std::invalid_argument("blocked_rows_sub: affine two-level weights, an act with derived sums");
    s.split(r.x, rxs, rxc, sub);
    Func blk = s.rfactor(ry, u);
    // the act's codes stay out of the inline (staged per sub-block, below)
    std::vector<Func> ac = decoders(rs, only({tag::codes}), 1);
    std::vector<Func> dec = decoders(rs, all_but({}), 0, 1), rest = decoders(rs, all_but({tag::codes}), 1);
    dec.insert(dec.end(), rest.begin(), rest.end());
    blk.update().eager_inline(dec);
    Func codes = blk.update().hoist_invariants()[0].change_type(Int(32));
    // per sub-block (v): its scales times its codes' dot (inner)
    Func part = codes.update().rfactor(rxs, v);
    Func inner = part.update().hoist_invariants()[0];
    Stage iu = inner.update();
    iu.split(rxc, rp, rxc, chunk).split(rxc, rq, ri, 4);
    Func quads = iu.rfactor(rq, q);
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor(ryi, bacc);
    LoopLevel at(acc, ryo);
    s.reorder(n, ryi).vectorize(n).unroll(ryi).unroll(m);
    for (Func f : {blk, codes}) {
        regs(f.compute_at(at).bound_storage(u, ts.interleave), {n}, {m, u});
    }
    // a loop over sub-block pairs (64 values: the same bytes' low and high
    // nibbles), each pair unrolled, as GGML's (fully unrolled, the codes'
    // loads are scheduled early and spill)
    RVar rxso("rxso"), rxsi("rxsi");
    codes.update().reorder(n, m, rxs, u).split(rxs, rxso, rxsi, 2).unroll(rxsi);
    LoopLevel per_sub(codes, rxsi);
    regs(part.compute_at(per_sub), {n}, {v, u, m});
    regs(inner.compute_at(per_sub), {n}, {v, u, m});
    inner.update().reorder(n, rq, v, u, m).unroll(rq);
    regs(quads.reorder_storage(q, n, v, u, m).compute_at(per_sub), {q, n}, {v, u, m, rp});
    quads.update().reorder(ri, q, n, rp, v, u, m).atomic().vectorize(ri);
    // the sub-block's act codes, in registers: each piece a lane of them
    auto calls = Internal::find_direct_calls(quads.function());
    for (Func a : ac) {
        if (calls.count(a.name())) {
            Func ai = a.in(quads);
            ai.compute_at(per_sub).vectorize(ai.args()[0]);
        }
    }
    Stage au = acc.update();
    au.reorder(n, bacc, m, ryo);
    regs(acc.compute_at(tile_at(out, st)), {n}, {bacc, m});
    schedule_sums(d, out, st, acc, n, m, ts, r, true, 16, true);
}

// An i8mm stage's 2 x 2 sub-tiles (smmla's results): n and m split in 2
// (nio, nii; mio, mii), guarded (this code is also in other tiles' branches,
// never run there, whose bounds must not grow past their tiles), the loops
// `order` (innermost first), each sub-tile's 4 lanes vectorized, the
// sub-tiles unrolled.
inline Stage sub_tiles(Stage s, Var n, Var m, const std::vector<VarOrRVar> &order) {
    Var nio("nio"), nii("nii"), mio("mio"), mii("mii");
    s.split(n, nio, nii, 2, TailStrategy::GuardWithIf).split(m, mio, mii, 2, TailStrategy::GuardWithIf);
    return s.reorder(order).vectorize(nii).vectorize(mii).unroll(nio).unroll(mio);
}

// f stored in sub-tiles: each 4 dense lanes, then the sub-tiles, then `outer`.
inline Func sub_tile_storage(Func f, Var n, Var m, std::vector<Var> outer = {}) {
    Var sno, sni, smo, smi;
    outer.insert(outer.begin(), {sni, smi, sno, smo});
    return f.split_storage(n, sno, sni, 2).split_storage(m, smo, smi, 2).reorder_storage(outer);
}

// The integer path on i8mm smmla (2 x 2 x 8 matrix-multiply tiles): nt x mt
// tiles of 2 x 2 sub-tiles, each 4 dense lanes of the intermediates' storage
// (split_storage), so the tile's accumulators stay in registers.
// `records`: the weights in records of ts.nt rows (tiles from 0 mod ts.nt,
// N a multiple), their mins across the rows (as blocked_rows_sub).
// `act_records`: the act too in whole records (q8_K.4x8).
inline void blocked_mmla(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r,
                         const std::vector<ApproximationResult> &rs, bool derived = false, int sub = 0, int act_sub = 0, bool records = false, bool act_records = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r, records);
    Var n = dot.args()[0], m = dot.args()[1], nio("nio"), nii("nii"), mio("mio"), mii("mii"), u("u"), v("v"), bacc("bacc");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rxk("rxk"), ryo("ryo"), ryi("ryi");
    bool aff = affine(rs);
    Sums d;
    // records: the sub-blocks' decoded scales (and mins) their own stage
    std::vector<Func> kept = records && sub ? scale_decoders(rs) : std::vector<Func>{};
    if (derived && derived_sums(out, st, s, r, rs, d, act_sub, records ? sub : 0, kept)) aff = false;
    sub ? s.split(r.x, rxs, rxc, sub).split(rxc, rxc, rxk, 8) : s.split(r.x, rxc, rxk, 8);
    Func blk = s.rfactor(ry, u);
    bool fsub = sub && !records && float_sub_scales(rs);
    Func rest, mid, codes = block_dot(blk, rs, !fsub, rest, mid, aff, aff ? std::vector<Func>{} : kept);
    // per sub-block (v): its scale times its codes' dot (inner), as blocked()
    Func part, inner;
    if (sub) {
        part = codes.update().rfactor(rxs, v);
        inner = part.update().hoist_invariants()[0];
        if (fsub) inner = inner.change_type(Int(32));
    }
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor(ryi, bacc);
    s.vectorize(n, 2, TailStrategy::GuardWithIf).unroll(n).unroll(m);
    for (Func f : {acc, blk, codes, mid, part, inner}) {
        if (!f.defined()) continue;
        Var x = f.args()[2];  // x: bacc or u
        std::vector<Var> outer = f.dimensions() > 3 ? std::vector<Var>{v, x} : std::vector<Var>{x};
        sub_tiles(sub_tile_storage(f, n, m, outer), n, m, {nii, mii, nio, mio, x}).unroll(x);
        // per block (per 2 rows, tall tiles keep the weight's codes live and spill)
        if (x.name() == u.name()) f.compute_at(acc, ryo).bound_storage(u, ts.interleave);
    }
    if (sub) {  // per block and sub-block: the tile's sub-block dots, then their scales
        sub_tiles(codes.update(), n, m, {nii, mii, nio, mio, rxs, u}).unroll(rxs).unroll(u);
        if (records) {
            // the scales' factor one 16-lane vector per sub-block (2 x 2
            // sub-tiles of the tile's columns): one shuffle of the decoded
            // scales, not one per sub-tile
            Var no("no"), ni("ni"), mo("mo"), mi("mi"), nm("nm"), nmo("nmo");
            part.update().split(n, no, ni, 2, TailStrategy::RoundUp).split(m, mo, mi, 2, TailStrategy::RoundUp).reorder({ni, mi, no, mo, v, u}).fuse(ni, mi, nm).fuse(nm, no, nmo).vectorize(nmo).unroll(mo).unroll(v).unroll(u);
        } else {
            sub_tiles(part.update(), n, m, {nii, mii, nio, mio, v, u}).unroll(v).unroll(u);
        }
        part.compute_at(codes, rxs);
        inner.compute_at(codes, rxs);
        // the sub-block's scales, decoded once across the record's rows
        for (Func k : kept) {
            if (!calls_any(part.function().update(0).values()[0], {k})) continue;
            Func ks = k.in(part);
            ks.compute_at(codes, rxs).vectorize(ks.args()[2], ts.nt);
            ks.unroll(ks.args()[0]).unroll(ks.args()[1]);
        }
    }
    acc.compute_at(tile_at(out, st));
    sub_tiles(acc.update(), n, m, {nii, mii, nio, mio, bacc, ryo}).unroll(bacc);
    for (Func f : {blk, mid}) {
        if (f.defined()) sub_tiles(f.update(), n, m, {nii, mii, nio, mio, u}).unroll(u);
    }
    sub_tiles((sub ? inner : codes).update(), n, m, {rxk, nii, mii, rxc, nio, mio, u}).unroll(rxc).unroll(u).atomic().vectorize(rxk);
    if (rest.defined()) {  // the act's block sum, once per act column (n vectorized)
        regs(rest.compute_at(acc, ryo).bound_storage(u, ts.interleave), {n}, {m, u});
        rest.update().reorder(rxk, rxc, n, m, u).atomic().vectorize(rxk).vectorize(rxc);
    }
    schedule_sums(d, out, st, acc, n, m, ts, r, true, 16, records, act_records);
    tables(rs, {blk, mid, codes, rest, part, inner}, LoopLevel(acc, ryo));
}

// The Funcs of e's encoder: the intermediates of the result that encodes e,
// minus its decode Funcs.
inline std::vector<Func> encoder_funcs(Func e, const std::vector<ApproximationResult> &rs) {
    std::vector<Func> dec, fs;
    for (const ApproximationResult &res : rs) {
        std::vector<Func> d = res.decode_funcs();
        dec.insert(dec.end(), d.begin(), d.end());
    }
    for (const ApproximationResult &res : rs) {
        for (Func f : res.encoded[0].name() == e.name() ? res.intermediates : std::vector<Func>{}) {
            if (std::none_of(dec.begin(), dec.end(), [&](const Func &d) { return d.name() == f.name(); })) fs.push_back(f);
        }
    }
    return fs;
}

// Whether f's update may be computed out of order (atomic()): not if it
// accumulates by a strict_float add, subtract, multiply or fma, which must
// keep its order (min and max may reorder).
inline bool reassociable(const Func &f) {
    using Internal::Call;
    for (const Expr &v : f.function().update(0).values()) {
        const Call *c = v.as<Call>();
        if (c && (c->is_intrinsic(Call::strict_add) || c->is_intrinsic(Call::strict_sub) || c->is_intrinsic(Call::strict_mul) || c->is_intrinsic(Call::strict_fma))) return false;
    }
    return true;
}

// An encoder computed per record (each block of values), vectorized: the
// Funcs with an element per value (e.g. the codes, packed bytes) across their
// elements, and its reductions (e.g. the block's scale) across the block,
// those Halide can reorder (not a strict_float sum, kept in order: q1_0's).
inline void encoder(Func e, const std::vector<ApproximationResult> &rs) {
    for (Func f : encoder_funcs(e, rs)) {
        if (f.name() == e.name()) continue;
        if (f.dimensions() > e.dimensions()) {
            f.compute_at(e, e.args()[0]).vectorize(f.args()[0]);
            for (int u = 0; u < f.num_update_definitions(); u++) {
                f.update(u).vectorize(f.args()[0]);
            }
        } else if (f.has_update_definition()) {
            f.compute_at(e, e.args()[0]);
            if (reassociable(f)) f.update().atomic().vectorize(f.rvars()[0]);
        }
    }
}

// The N = M = 1 schedule (GGML's vec_dot). Activations encoded inside the
// pipeline (`staged`) are encoded once, up front. `interleave` blocks per
// iteration (blocked weights; a per-format parameter, formats.cmake).
inline void vec_dot(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t, const Approximation &acc = Approximation(), int interleave = 2,
                    bool sublanes = false, bool gpr_scales = false) {
    for (Func e : staged) {
        e.compute_root();
        encoder(e, rs);
        if (fmt.act_block == 1) e.vectorize(e.args()[0], t.natural_vector_size<float>() * 4, TailStrategy::GuardWithIf);
    }
    out.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);  // N = M = 1
    if (fmt.block == 1) {
        acc.defined() ? flat_tree(out, 0, dots[0], Expr(), {1, 1, 4}, r, acc, staged) : flat(out, 0, dots[0], Expr(), {1, 1, 4}, r, t);
        return;
    }
    blocked(out, 0, dots[0], Expr(), {1, 1, interleave}, r, fmt.act_block == fmt.block, rs, t, true, fmt.sub, fmt.act_sub, sublanes, gpr_scales);
}

// The Metal tiles of out: threadgroups (gpu_blocks) of `sg` simdgroups
// (gpu_threads ns), each `rows(cols)` rows (ni, unrolled) of `cols` columns
// (mi, unrolled), for the widest cols <= 8 that M allows (gemv: nr rows of
// one column); N < nr * sg, a row per threadgroup. Edge tiles shift inwards.
// dot is computed per output (ni): warp allocations of one value.
inline void metal_tiles(Func out, Func dot, int nr, int sg, const std::function<int(int)> &rows) {
    Var n = out.args()[0], m = out.args()[1], no("no"), ns("ns"), ni("ni"), mo("mo"), mi("mi");
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
        tile(big.specialize(M >= cols), rows(cols), sg, cols);
    }
    tile(big, nr, sg, 1);
    tile(Stage(out), 1, 1, 1);
    dot.compute_at(out, ni);
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
    Var n = out.args()[0], m = out.args()[1], ns("ns"), uh("uh"), ub("ub"), u("u"), l("l");
    RVar rx = r.x, ry = r.y, rxn("rxn"), rxh("rxh"), rxi("rxi"), ryo("ryo"), ryi("ryi"), rl("rl");
    Expr M = out.output_buffer().dim(1).extent();
    metal_tiles(out, dot, nr, sg, [](int cols) { return cols > 4 ? 2 : 4; });
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

// Metal on weights of one value per record (floats: no blocks), as GGML's
// kernel_mul_mv_f16_f32_4: metal_tiles of 2 rows (gemv) or 4 rows of up to 8
// columns. A lane takes `c` contiguous values of every 32 * c (the
// simdgroup's: one coalesced stream per row; the Metal compiler merges the
// unrolled loads into half4 / float4), accumulating each output in a scalar
// register (float4 accumulators spill for M > 1); the lanes' sums then reduce
// across the simdgroup (gpu_lanes) and lane 0 writes. The K tail is guarded.
// Staged acts (f32 encoded per value) get a kernel of their own: inlined,
// every tile of rows would encode its columns again.
inline void metal_flat(Func out, Func dot, const RDom &r, const std::vector<Func> &staged, int nr = 2, int sg = 2, int c = 16) {
    Var n = out.args()[0], m = out.args()[1], ns("ns"), l("l");
    RVar rx = r.x, ry = r.y, ryo("ryo"), rl("rl"), ryi("ryi");
    metal_tiles(out, dot, nr, sg, [](int) { return 4; });
    Stage s = dot.update();
    s.split(ry, ryo, ryi, 32 * c, TailStrategy::GuardWithIf).split(ryi, rl, ryi, c);
    Func lanes = s.rfactor(rl, l);
    s.atomic().gpu_lanes(rl);
    lanes.compute_at(out, ns).gpu_lanes(l).unroll(n).unroll(m);
    lanes.update().reorder(rx, ryi, n, m, ryo, l).unroll(ryi).unroll(n).unroll(m).gpu_lanes(l);
    for (Func e : staged) {
        Var k = e.args()[0], em = e.args()[1], ko("ko"), kv("kv");
        e.compute_root().split(k, ko, k, 256, TailStrategy::GuardWithIf).split(k, k, kv, 4).vectorize(kv).gpu_blocks(ko, em).gpu_threads(k);
    }
}

// Rowwise formats (schemes/kleidiai.h: weights qsi8cx, acts qai8dx): one
// scale per row, so the codes' dot accumulates in int32 over the whole of K.
// dot's increment, the weight's codes times its scale times the act's codes
// plus its offset times its scale (s, out's stage tile under `cond`), splits
// on the act's codes (its ports tagged codes and their decode): one Func
// the codes' dot (int32), the other the weight's code sum, read from its
// derived port (read_port: the row sums its encoder stored),
// each times its scales once per output. Returns the codes' dot.
inline Func rowwise_dot(Stage s, const std::vector<ApproximationResult> &rs) {
    std::vector<Func> act = decoders(rs, only({tag::codes}), 1);
    for (size_t i = 1; i < rs.size(); i++) {
        for (size_t j = 0; j < rs[i].encoded.size(); j++) {
            if (rs[i].encoded_ports[j].tags == ApproximationTags{tag::codes}) act.push_back(rs[i].encoded[j]);
        }
    }
    s.eager_inline(decoders(rs, all_but({tag::codes})));
    FuncVec parts = s.hoist_invariants({act});
    if (!parts[1].defined()) throw std::invalid_argument("rowwise_dot: no terms free of the act's codes in " + s.name());
    Func codes = parts[0].change_type(Int(32)), sum = parts[1].change_type(Int(32));
    if (!read_port(sum, rs[0].derived_ports(), parts[1])) throw std::invalid_argument("rowwise_dot: the weight's code sum matches none of its derived ports");
    return codes;
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
    Func codes = rowwise_dot(s, rs);
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
    Func codes = rowwise_dot(s, rs);
    s.vectorize(dot.args()[0]);  // the scaling, once per output
    Var n = dot.args()[0], m = dot.args()[1], nio("nio"), nii("nii"), mio("mio"), mii("mii");
    RVar rx = r.x, ry = r.y, rxc("rxc"), rxk("rxk");
    sub_tiles(sub_tile_storage(codes, n, m), n, m, {nii, mii, nio, mio});
    codes.compute_at(tile_at(out, st));
    if (guard_m) {
        // (a constant under cond; M = 1, the gemv, needs only its row)
        codes.bound_extent(m, select(out.output_buffer().dim(1).extent() > 1, ts.mt, 1));
        // the guarded tail tile's rows, else a heap allocation per tile
        dot.bound_storage(m, ts.mt);
    }
    Stage cu = codes.update();
    cu.split(rx, rxc, rxk, 8);
    sub_tiles(cu, n, m, {rxk, nii, mii, nio, mio, rxc, ry}).unroll(rxc).atomic().vectorize(rxk);
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
    for (Func f : encoder_funcs(e, rs)) {
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

// mul_mat on rowwise formats: gemm (M > 1, N > 1) on smmla tiles where the
// target has i8mm, 8 x 8 (16 accumulators: enough independent smmla chains to
// fill the pipes, as KleidiAI's 16 x 4), else 4 x 8; on acts encoded here,
// tiles of whole groups of 8 rows (the last guarded), any M > 1; on
// pre-quantized acts, shifted inwards (4 x 2 for M < 8); gemv
// tiles of 4 rows (a record of interleaved weights; planar: N >= 4, else
// single rows), in tasks of 16 rows.
inline void rowwise_mul_mat(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt,
                            const std::vector<ApproximationResult> &rs, const std::vector<Func> &staged, const Target &t) {
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    if (dots.size() > 1 || fmt.act_rows > 1) throw std::invalid_argument("rowwise: act records of one row");
    int chunk = fmt.rows > 1 ? fmt.chunk : 0;
    for (Func e : staged) {
        rowwise_encoder(e, out, rs, M, t.has_feature(Target::ARMI8MM));
    }
    Func dot = dots[0];
    Tiles mmla8x8{8, 8, 1, 32, 32, 2}, mmla4x8{4, 8, 1, 32, 32, 2}, mmla4x2{4, 2, 1, 32, 32, 2}, rows{4, 1, 1, 16}, single{1, 1, 1, 16};
    auto fits = [&](const Tiles &ts) { return N >= ts.nt && M >= ts.mt; };
    if (t.has_feature(Target::ARMI8MM)) {
        // records of 4 rows: whole 8-row tiles only where N allows. Acts
        // encoded here, in groups of 8 rows (padded): whole groups (records:
        // N >= 4, so the gemv after learns M = 1)
        Expr whole8 = chunk == 0 || N % 8 == 0;
        Chain enc{{N >= 8 && M > 1 && whole8, mmla8x8}, {chunk ? M > 1 : N >= 4 && M > 1, mmla4x8}};
        Chain pre{{fits(mmla8x8) && whole8, mmla8x8}, {fits(mmla4x8), mmla4x8}, {fits(mmla4x2), mmla4x2}};
        for (auto [c, ts] : staged.empty() ? pre : enc) {
            rowwise_mmla(out, 0, dot, c, ts, r, chunk > 0, rs, !staged.empty());
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
// r = fmt.act_rows > 1 rows, out's first update is the rows in whole records
// (dots[1]) on gemm tiles starting on a record, its second the others
// (dots[0], < r rows) on gemv tiles.
inline void mul_mat(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t, const Approximation &acc = Approximation(), int interleave = 2,
                    bool gpr_scales = false) {
    if (t.has_feature(Target::Metal)) {
        if (acc.defined()) throw std::invalid_argument("metal: dots accumulated in f32");
        if (fmt.act_rows > 1 || fmt.rows > 1) throw std::invalid_argument("metal: weights and acts one row per record");
        if (fmt.block == 1) {
            metal_flat(out, dots[0], r, staged);
            return;
        }
        if (!staged.empty()) throw std::invalid_argument("metal: blocked act formats stored as computed");
        metal_rows(out, dots[0], r, rs);
        return;
    }
    if (fmt.rowwise) {
        rowwise_mul_mat(out, dots, r, fmt, rs, staged, t);
        return;
    }
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    bool integer = fmt.act_block == fmt.block, whole = fmt.act_rows > 1;
    Expr Mr = M / fmt.act_rows * fmt.act_rows;
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
            Expr B = r.y.extent(), recs = whole ? Mr / fmt.act_rows : M;
            std::vector<int> cs;
            for (int c = 1; c <= 16; c++) {
                cs.push_back(c);
            }
            Expr chunks = balanced(cs, 4096, [&](int c) { return std::pair{recs * c, (B + c - 1) / c * fmt.act_block * fmt.act_rows}; });
            s.split(b, bo, b, (B + chunks - 1) / chunks, TailStrategy::GuardWithIf).fuse(rec, bo, t).parallel(t);
        }
        encoder(e, rs);
        if (fmt.act_block == 1) {  // values: vectorized (also for M = 1)
            s.vectorize(b, fv, TailStrategy::GuardWithIf);
            e.vectorize(b, fv, TailStrategy::GuardWithIf);
        }
    }
    if (fmt.block == 1) {
        // Float weights (no blocks): 4 x 2 tiles (each vector of a row's
        // weights used for both act columns, each act vector for 4 rows), row
        // pairs, single rows. gemv (M = 1) takes single rows: one weight
        // stream (row pairs are two, a row apart: 0.65x GGML at K = 14336).
        if (whole || fmt.rows > 1) throw std::invalid_argument("weights without blocks: one row per record, acts too");
        Tiles fg{4, 2, 1, 32, 32, 2}, fp{2, 1, 2, 16}, fr{1, 1, 4, 16};
        // (GGML's tree: four f16 vectors per output; 4 x 2 tiles spill)
        if (acc.defined()) fg = {2, 2, 1, 32, 32, 2};
        for (auto [c, ts] : Chain{{N >= fg.nt && M >= fg.mt, fg}, {N >= fp.nt && M > 1, fp}, {Expr(), fr}}) {
            acc.defined() ? flat_tree(out, 0, dots[0], c, ts, r, acc, staged) : flat(out, 0, dots[0], c, ts, r, t);
        }
        return;
    }
    Tiles mmla8{4, 8, 1, 32, 32, 2}, gemm{4, 4, 1, 32, 32, 2}, pairs{2, 1, 2, 16}, rows{1, 1, 2, 16};
    bool mmla_ok = integer && t.has_feature(Target::ARMI8MM);
    // Weight records in 4-code pieces: by-element sdot on the same tiles (on M3,
    // KleidiAI's dotprod gemm beats both i8mm ones)
    bool lanes = whole && integer && fmt.rows > 1 && fmt.chunk == 4, tiled = mmla_ok || lanes;
    // Two-level records of several rows (q4_K_8x8): the rest on their gemv;
    // with i8mm, on whole act records too (q8_K.4x8), one record per tile
    bool recs = integer && fmt.rows > 1 && affine(rs), rmmla = mmla_ok && recs;
    Tiles rm{fmt.rows, 4, 1, 32, whole ? 128 : 32, 2};
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
    Chain tiles{{Mr == wide.mt && fits(wide, Mr), wide}, {Mr <= t8.mt, t8}, {padded(t16) == fewest, t16}, {padded(t20) == fewest, t20}, {Expr(), t12}};
    if (whole && rmmla) {  // N a multiple of the records' rows
        dr.bound_extent(dr.args()[1], rm.mt);
    } else if (whole) {  // odd N first, as tall as t16; the bound is then constant in each branch
        blocked(out, 1, dr, odd_n, {1, tiled ? t16.mt : gemm.mt, 1, 32, 32, 2}, r, integer, rs, t);
        Expr mt = t12.mt;
        for (size_t i = tiles.size() - 1; i-- > 0;) {
            mt = select(tiles[i].first, tiles[i].second.mt, mt);
        }
        dr.bound_extent(dr.args()[1], tiled ? select(odd_n, t16.mt, mt) : Expr(gemm.mt));
    }
    if (whole && tiled && !rmmla) {
        for (auto [c, ts] : tiles) {
            lanes ? blocked_rows(out, whole, dr, c, ts, r, fmt.chunk, rs, fmt.act_rows) : blocked_mmla(out, whole, dr, c, ts, r, rs);
        }
    } else if (rmmla) {
        // whole records of rows (from 0 mod their rows: their addresses dense)
        // (dr's act is the last result: its derived ports, rs[1]'s for derived_sums)
        std::vector<ApproximationResult> rr = rs;
        if (whole) std::swap(rr[1], rr.back());
        blocked_mmla(out, whole, dr, whole ? Expr() : fits(rm, M), rm, r, rr, true, fmt.sub, fmt.act_sub, true, whole);
    } else if (mmla_ok) {
        blocked_mmla(out, whole, dr, fits(mmla8, M), mmla8, r, rs, true, fmt.sub, fmt.act_sub);
    }
    // (records without i8mm: their gemv per column; blocked()'s tiles gather)
    if (!(recs && !whole) && (!whole || !tiled)) blocked(out, whole, dr, whole ? Expr() : fits(gemm, M), gemm, r, integer, rs, t, !whole, fmt.sub, fmt.act_sub);
    // The rows past Mr (fewer than a record's): no tasks when there are none.
    Expr rest = whole ? M % fmt.act_rows != 0 : Expr();
    if (integer && fmt.rows > 1) {  // N is a multiple of the records' rows
        Tiles rt{fmt.rows, 1, 2, 16, 0, 0, rest};
        // (a two-level record, 8 rows of q4_K: 1152 bytes, one per iteration)
        if (affine(rs)) rt.interleave = 1;
        affine(rs) ? blocked_rows_sub(out, 2 * whole, dots[0], Expr(), rt, r, fmt.chunk, rs, fmt.sub, fmt.act_sub) : blocked_rows(out, 2 * whole, dots[0], Expr(), rt, r, fmt.chunk, rs);
        return;
    }
    pairs.busy = rows.busy = rest;
    pairs.interleave = rows.interleave = interleave;
    // A table's decode, per row pair, outweighs sharing the act's loads
    if (decoders(rs, all_but({}), 0, SIZE_MAX, true).empty()) blocked(out, 2 * whole, dots[0], fits(pairs, 1), pairs, r, integer, rs, t, true, fmt.sub, fmt.act_sub, false, gpr_scales);
    blocked(out, 2 * whole, dots[0], Expr(), rows, r, integer, rs, t, true, fmt.sub, fmt.act_sub, false, gpr_scales);
}

}  // namespace ggml
