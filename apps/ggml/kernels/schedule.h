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

// Does e call any of fs?
inline bool calls_any(const Expr &e, const std::vector<Func> &fs) {
    bool found = false;
    Internal::visit_with(e, [&](auto *self, const Internal::Call *c) {
        found |= std::any_of(fs.begin(), fs.end(), [&](const Func &f) { return c->name == f.name(); });
        self->visit_base(c);
    });
    return found;
}

// Whether f's update reads a bit field at a position that varies with v
// other than with v mod `period` (q5_K's high bits: sub-block s's in bit s
// of its bytes; not q4_K's nibbles, by sub-block parity, for period 2): a
// loop over v in steps of `period` would shift by a variable amount, which
// Halide widens.
inline bool bit_position_varies(const Expr &e, Var v, int period) {
    using namespace Internal;
    bool varies = false;
    visit_with(e, [&](auto *self, const Call *c) {
        if ((c->is_intrinsic(Call::shift_right) || c->is_intrinsic(Call::shift_left)) && expr_uses_var(c->args[1], v.name())) {
            Expr a = c->args[1], b = substitute(v.name(), Expr(v) + period, a);
            varies |= !can_prove(a == b);
        }
        self->visit_base(c);
    });
    return varies;
}

inline bool bit_position_varies(Func f, Var v, int period) {
    return bit_position_varies(Internal::remove_promises(Internal::remove_likelies(Internal::substitute_in_all_lets(f.update_value()))), v, period);
}

// The bits of v below `group` (a power of 2), innermost first: those more of
// f's bit fields' positions vary with first (ties: the lower). Sub-blocks
// differing in such a bit read the same bytes (q6_K's 128 values: bit 2, a
// byte's low and high nibbles, then bit 1, the high bits' pairs; q5_K's:
// bit 0, then 1 and 2), so iterated innermost, their loads are reused while
// live.
// (counts: per bit, how many of the bit fields' positions vary with it.)
using Points = std::vector<std::map<std::string, Expr>>;
inline std::vector<int> sharing_order(const Expr &e, const Points &points, Var v, int group, std::vector<int> *counts = nullptr) {
    using namespace Internal;
    int bits = 0;
    while ((1 << bits) < group)
        bits++;
    std::vector<int> count(bits, 0), order(bits);
    visit_with(e, [&](auto *self, const Call *c) {
        if ((c->is_intrinsic(Call::shift_right) || c->is_intrinsic(Call::shift_left)) && expr_uses_var(c->args[1], v.name())) {
            for (int b = 0; b < bits; b++) {
                bool varies = false;
                for (int x = 0; x < group && !varies; x++) {
                    for (size_t p = 0; p < points.size() && !varies && !(x >> b & 1); p++) {
                        Expr a = substitute(points[p], substitute(v.name(), x, c->args[1]));
                        Expr a2 = substitute(points[p], substitute(v.name(), x | 1 << b, c->args[1]));
                        varies = !can_prove(a == a2);
                    }
                }
                count[b] += varies;
            }
        }
        self->visit_base(c);
    });
    for (int b = 0; b < bits; b++) {
        order[b] = b;
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return count[a] > count[b]; });
    if (counts) *counts = count;
    return order;
}

inline std::vector<int> sharing_order(Func f, Var v, int group, std::vector<int> *counts = nullptr) {
    using namespace Internal;
    Expr e = remove_promises(remove_likelies(substitute_in_all_lets(f.update_value())));
    // the shift at each value of f's update's (constant) reduction domain
    Points points{{}};
    for (const ReductionVariable &rv : f.function().update(0).schedule().rvars()) {
        auto mn = as_const_int(simplify(rv.min)), ex = as_const_int(simplify(rv.extent));
        if (!mn || !ex) continue;
        Points next;
        for (const auto &p : points) {
            for (int64_t i = 0; i < *ex; i++) {
                next.push_back(p);
                next.back()[rv.var] = Expr((int)(*mn + i));
            }
        }
        points.swap(next);
    }
    return sharing_order(e, points, v, group, counts);
}

// Whether some bit of a sub-block's index no bit field's position varies
// with lies below one some do (counts: sharing_order's).
inline bool bit_beneath(const std::vector<int> &counts) {
    for (size_t b = 0; b < counts.size(); b++) {
        for (size_t c = b + 1; c < counts.size(); c++) {
            if (counts[b] == 0 && counts[c] > 0) return true;
        }
    }
    return false;
}

// The values of x (in sub-blocks of `sub`, `subs` of them) over which f's
// update's bit fields' positions (fs inlined) repeat, if within them
// bit_beneath (q6_K's 128: bit 0, the 16-value halves of 32 a byte's codes
// span, below the nibbles' and high bits' pairs, 2 and 1); else 0.
inline int bit_period(Func f, const RVar &x, int sub, int subs, const std::vector<Func> &fs) {
    using namespace Internal;
    Expr e = f.update_value();
    for (bool again = true; again;) {
        again = false;
        for (Func g : fs) {
            if (!calls_any(e, {g})) continue;
            e = inline_function(e, g.function());
            again = true;
        }
    }
    Var v("v"), y("y");
    e = remove_promises(remove_likelies(substitute_in_all_lets(substitute(x.name(), v * sub + y, e))));
    Points points;
    for (int i = 0; i < sub; i++) {
        points.push_back({{y.name(), i}});
    }
    int group = 2;
    while (group < subs && bit_position_varies(e, v, group))
        group *= 2;
    std::vector<int> counts;
    sharing_order(e, points, v, group, &counts);
    return group < subs && bit_beneath(counts) ? group * sub : 0;
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
    int per = 1;  // blocks per weight block (q1_0's 128 values on q8_0 acts: 4)
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

// Whether a kernel decodes bit fields through constant tables (Tabulate): on
// CPUs. A table moves the decode off the SIMD units, which bound the dot
// (Apple M3 Max, q5_0, one thread, medians over link layouts: vec_dot from
// 0.80x GGML's speed to 1.01x, gemv on single rows from 0.86x to 1.0x, gemm
// M = 32 from 2.2x to 2.7x).
inline bool use_tables(const Target &t) {
    return !t.has_gpu_feature();
}

// The table decoders that tables() stages: all but tables of one value per
// index (nvfp4's scales), which stay inline: each read a scalar load where
// it is used.
inline std::vector<Func> staged_tables(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> fs;
    for (Func f : decoders(rs, all_but({}), 0, SIZE_MAX, true)) {
        if (table_of(f).dim(0).extent() != 1) fs.push_back(f);
    }
    return fs;
}

// The table decodes at `at` (per block group), each a copy for its calls from
// `consumers` (a pipeline computes them per kernel): unrolled over all but
// their first dimension, split by the table's rows, so each read of a row is
// a dense load.
inline void tables(const std::vector<ApproximationResult> &rs, const std::vector<Func> &consumers, LoopLevel at) {
    for (Func f : staged_tables(rs)) {
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

// e as a sum of terms (Add and Sub nodes), each with its sign: its constant
// terms summed into c, the rest into `terms`.
inline void sum_terms(const Expr &e, int sign, std::vector<std::pair<Expr, int>> &terms, int64_t &c) {
    if (const auto *a = e.as<Internal::Add>()) {
        sum_terms(a->a, sign, terms, c);
        sum_terms(a->b, sign, terms, c);
    } else if (const auto *s = e.as<Internal::Sub>()) {
        sum_terms(s->a, sign, terms, c);
        sum_terms(s->b, -sign, terms, c);
    } else if (auto k = Internal::as_const_int(e)) {
        c += sign * *k;
    } else if (auto k = Internal::as_const_uint(e)) {
        c += sign * (int64_t)*k;
    } else {
        terms.emplace_back(e, sign);
    }
}

// The weight's integer code decoders that add a constant (an offset: tq2_0's
// field - 1, q6_K's high bits - 32).
inline std::vector<Func> offset_decoders(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> fs;
    for (Func f : decoders(rs, only({tag::codes}), 0, 1)) {
        const Internal::Function &fn = f.function();
        if (fn.has_update_definition() || fn.outputs() != 1 || !fn.output_types()[0].is_int_or_uint()) continue;
        std::vector<std::pair<Expr, int>> terms;
        int64_t c = 0;
        sum_terms(fn.definition().values()[0], 1, terms, c);
        if (c && !terms.empty()) fs.push_back(f);
    }
    return fs;
}

// constant_integer_bounds(e), through selects (the union of their values')
inline Internal::ConstantInterval select_bounds(const Expr &e) {
    using namespace Internal;
    Scope<ConstantInterval> scope;
    std::vector<std::string> names;
    Expr x = mutate_with(e, [&](auto *self, const Select *op) -> Expr {
        Expr t = self->mutate(op->true_value), f = self->mutate(op->false_value);
        if (!op->type.is_int_or_uint()) return Select::make(op->condition, t, f);
        ConstantInterval b = constant_integer_bounds(t, scope);
        b.include(constant_integer_bounds(f, scope));
        names.push_back(unique_name('s'));
        scope.push(names.back(), b);
        return Variable::make(op->type, names.back());
    });
    return constant_integer_bounds(x, scope);
}

// v (a stage's value) with each widening cast of a sum of the weight's codes
// and a constant (cast<float>(int8(field) + -1), the offset decoders and the
// code decoders between v and them inlined) split into the cast of the rest
// plus the constant, where the decoded codes' bounds prove neither sum wraps
// (so the value is unchanged, bit for bit): the constant's term then hoists
// out of the codes' dot as an affine weight's min does (derived_sums()).
// Undefined if none splits.
inline Expr split_offsets(const Expr &v, const std::vector<ApproximationResult> &rs) {
    using namespace Internal;
    std::vector<Func> offs = offset_decoders(rs);
    if (offs.empty()) return Expr();
    std::vector<Func> codes = decoders(rs, only({tag::codes}), 0, 1), path = offs;
    for (bool grew = true; grew;) {
        grew = false;
        for (Func f : codes) {
            if (std::any_of(path.begin(), path.end(), [&](const Func &p) { return p.name() == f.name(); })) continue;
            const auto &vals = f.function().definition().values();
            if (!f.function().has_update_definition() && std::any_of(vals.begin(), vals.end(), [&](const Expr &e) { return calls_any(e, path); })) {
                path.push_back(f);
                grew = true;
            }
        }
    }
    auto inlined = [](const Expr &e, const std::vector<Func> &fs) {
        Inliner in;
        for (Func f : fs) {
            if (!f.function().has_update_definition()) in.add(f.function());
        }
        return substitute_in_all_lets(in(e));
    };
    std::vector<Func> all = codes;
    for (Func f : decoders(rs, only({tag::codes}), 0, 1, true)) {
        all.push_back(f);
    }
    // bounds of a sum of terms, each partial sum within t: else undefined
    auto fits = [&](const std::vector<std::pair<Expr, int>> &terms, int64_t c, Type t) {
        ConstantInterval range = ConstantInterval::bounds_of_type(t), acc = ConstantInterval::single_point(0);
        bool ok = true;
        for (const auto &[e, sign] : terms) {
            ConstantInterval b = select_bounds(simplify(inlined(e, all)));
            sign > 0 ? acc += b : acc -= b;
            ok &= range.contains(acc);
        }
        acc += c;
        return ok && range.contains(acc);
    };
    bool split = false;
    Expr e = mutate_with(inlined(v, path), [&](auto *self, const Cast *op) -> Expr {
        Expr x = self->mutate(op->value);
        Type t = op->type, xt = x.type();
        bool exact = t.is_float() ? xt.bits() < t.bits() && xt.bits() <= 16 : t.is_int() && xt.bits() < t.bits();
        std::vector<std::pair<Expr, int>> terms;
        int64_t c = 0;
        if (xt.is_int_or_uint() && exact) sum_terms(x, 1, terms, c);
        if (!c || terms.empty() || !calls_any(x, codes)) return Cast::make(t, x);
        // no node of the original wraps
        bool ok = true;
        std::function<void(const Expr &)> nodes = [&](const Expr &y) {
            const auto *a = y.as<Add>();
            const auto *s = y.as<Sub>();
            if (!a && !s) return;
            std::vector<std::pair<Expr, int>> ts;
            int64_t k = 0;
            sum_terms(y, 1, ts, k);
            ok &= fits(ts, k, xt);
            nodes(a ? a->a : s->a);
            nodes(a ? a->b : s->b);
        };
        nodes(x);
        // the rest, positive terms first
        std::stable_partition(terms.begin(), terms.end(), [](const auto &p) { return p.second > 0; });
        if (!ok || !fits(terms, 0, xt) || terms[0].second < 0) return Cast::make(t, x);
        Expr rest = terms[0].first;
        for (size_t i = 1; i < terms.size(); i++) {
            rest = terms[i].second > 0 ? Add::make(rest, terms[i].first) : Sub::make(rest, terms[i].first);
        }
        split = true;
        return Cast::make(t, rest) + make_const(t, c);
    });
    return split ? e : Expr();
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
// the values' dimensions: (sub-block, block, row); decoded or read from a
// table)?
inline bool float_sub_scales(const std::vector<ApproximationResult> &rs) {
    for (bool tables : {false, true}) {
        for (Func f : decoders(rs, only({tag::scale}), 0, 1, tables)) {
            if (f.type().is_float() && f.dimensions() == 3) return true;
        }
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

// f's update with each cast of a widening_mul to a wider integer type the
// product of the operands' casts (the same value: hoist_invariants then
// splits it).
inline void distribute_widening(Func f) {
    using namespace Internal;
    for (Expr &v : f.function().update(0).values()) {
        v = mutate_with(v, [](auto *self, const Cast *op) -> Expr {
            Expr x = self->mutate(op->value);
            const auto *c = x.as<Call>();
            if (c && c->is_intrinsic(Call::widening_mul) && op->type.is_int() && op->type.bits() > x.type().bits()) {
                return Cast::make(op->type, c->args[0]) * Cast::make(op->type, c->args[1]);
            }
            return Cast::make(op->type, x);
        });
    }
}

// The definition s (a stage of f: an update, or a specialization of one) is.
inline Internal::Definition stage_definition(Func f, const Stage &s) {
    Internal::Function fn = f.function();
    Expr v = s.values()[0];
    std::function<std::optional<Internal::Definition>(Internal::Definition &)> find = [&](Internal::Definition &d) -> std::optional<Internal::Definition> {
        if (d.values()[0].same_as(v)) return d;
        for (Internal::Specialization &sp : d.specializations()) {
            if (auto r = find(sp.definition)) return r;
        }
        return std::nullopt;
    };
    for (size_t i = 0; i < fn.updates().size(); i++) {
        if (auto r = find(fn.update((int)i))) return *r;
    }
    throw std::invalid_argument("stage_definition: " + s.name() + " is no stage of " + f.name());
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
// A weight whose codes decode to a field plus a constant (tq2_0's field - 1,
// q6_K's - 32: split_offsets()) is treated alike, the constant its min: the
// act's sums are then times the constant (and any per-sub-block factor, q6_K's
// scales), as GGML subtracts offset * bsums rather than the offset per value.
// (`fold`: a per-format parameter, as GGML's q3_K subtracts its offset per
// value.) Returns false (dot untouched) if the weight is neither affine nor
// offset or the act has no derived port.
struct Sums {
    Func sums, blk, sum, part, pair;
    bool sub_sums = false;  // d.sum sums the act's sub-block sums (no part)
    bool offsets = false;   // a constant code offset's (split_offsets())
};
inline bool derived_sums(Func out, int st, Func dot, Stage &s, const RDom &r, const std::vector<ApproximationResult> &rs, Sums &d, int act_sub = 0, int sub = 0, const std::vector<Func> &kept = {}, bool fold = true) {
    d = Sums();
    if (rs.size() < 2 || rs[1].derived_ports().empty()) return false;
    std::vector<Func> dec;
    for (Func f : decoders(rs, all_but({tag::codes}), 0, 1)) {
        if (std::none_of(kept.begin(), kept.end(), [&](const Func &k) { return k.name() == f.name(); })) dec.push_back(f);
    }
    bool offsets = false;
    if (fold && !offset_decoders(rs).empty()) {
        Internal::Inliner probe;
        for (Func f : dec) {
            probe.add(f.function());
        }
        offsets = split_offsets(probe(s.value()), rs).defined();
    }
    if (!offsets && !affine(rs)) return false;
    d.offsets = offsets;
    s.eager_inline(dec);
    if (offsets) stage_definition(dot, s).values()[0] = split_offsets(s.value(), rs);
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
        // (an int8 weight factor, q6_K's scales: their product with the
        // act's codes a widening_mul, which hoist_invariants can't split)
        distribute_widening(d.sum);
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
        // (a constant offset's term, split_offsets(): no factor per act
        // sub-block, so the block's sum is that of the port's values)
        const std::vector<DerivedPort> &ports = rs[1].derived_ports();
        bool factor = std::none_of(ports.begin(), ports.end(), [&](const DerivedPort &p) { return match_derived_port(d.part, p); });
        Func term = factor ? d.part.update().hoist_invariants()[0] : d.part, reader = factor ? d.part : d.sum;
        if (pairs) {
            // the act's codes: two sub-blocks' sum fits int16, as the port
            d.pair = term.change_type(Int(16));
            reader = d.pair;
            term = d.pair.update().rfactor(rxh, Var("h"));
        }
        if (!read_port(term, rs[1].derived_ports(), reader)) {
            throw std::invalid_argument("derived_sums: the act's sub-block sums in " + s.name() + " match none of its derived ports");
        }
        if (!factor) {
            d.part = Func();
            d.sub_sums = true;
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
    } else if (d.part.defined() || d.sub_sums) {
        // per act sub-block, vectorized across them: its factor times its sum
        RVar rxt("rxt"), rxj("rxj");
        d.sum.compute_at(blocks).unroll(u).unroll(n).unroll(m);
        d.sum.update().atomic().vectorize(rxt).unroll(u).unroll(n).unroll(m);
        if (d.sub_sums) return;
        Var t = d.part.args().back(), to("to"), ti("ti");
        d.part.compute_at(blocks).reorder_storage(t, u, n, m);
        if (d.offsets) {
            // an offset's factor (q6_K's int8 scales) times the act's int16
            // sums, 8 lanes a step: sshll then smull / smull2 (not 16, whose
            // high half's sign extend LLVM shuffles)
            d.part.split(t, to, ti, 8);
            d.part.update().split(t, to, ti, 8);
            regs(d.part, {ti}, {to, u, n, m});
        } else {
            regs(d.part, {t}, {u, n, m});
        }
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

// blocked()'s two-level weights with a float scale per sub-block (nvfp4's)
// whose blocks (the act's, the reduction's) hold `nsub` sub-blocks, fewer
// than an sdot's lanes, on tiles of ts.interleave blocks that fill them: as
// GGML's NEON nvfp4 vec_dot, the lanes are the (sub-block, block) of a block
// group, each its sub-block's int32 dot of the codes, then its scale (the
// weight's sub-block scale times the act's block scale) applied as one
// vector multiply-add per group. A sub-block's dot: its codes in pieces of
// `ql` quads, each piece of the block's sub-blocks one sdot (lanes: quad,
// sub-block; the codes of a piece of every sub-block one dense run of the
// record, as GGML's low and high nibbles), accumulated over the pieces, then
// its quads summed (pairwise adds across the group's blocks).
inline void blocked_sub_lanes(Func out, int st, Stage s, Var n, Var m, const RDom &r, const Tiles &ts, int sub, int nsub, int vec, int per,
                              const std::vector<ApproximationResult> &rs) {
    Var lane("lane"), u("u"), q("q"), bacc("bacc"), lu("lu"), qlv("ql");
    RVar ry = r.y, rxs("rxs"), rxh("rxh"), rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    const int ql = vec / per / nsub;
    s.split(r.x, rxs, rxc, sub).split(rxc, rxh, rxc, ql * per).split(rxc, rxo, rxi, per);
    Func blk = s.rfactor({{rxs, lane}, {ry, u}});
    blk.bound(lane, 0, nsub);
    Func rest, mid, codes = block_dot(blk, rs, true, rest, mid, false);
    Func quads = codes.update().rfactor(rxo, q);
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = sum_tree(s, {{{rxs, lane}, {ryi, bacc}}, {{rxs, lane}}}, tile_at(out, st), n, m, ryo, 2)[0];
    s.atomic().vectorize(rxs).unroll(n).unroll(m);
    // per block group: the scales and the sub-blocks' dots, a vector across
    // (sub-block, block)
    for (Func f : {blk, codes}) {
        f.compute_at(acc, ryo).reorder_storage(lane, u, n, m).bound_storage(u, ts.interleave);
        f.reorder(lane, u, n, m).fuse(lane, u, lu).vectorize(lu).unroll(n).unroll(m);
    }
    blk.update().reorder(lane, u, n, m).fuse(lane, u, lu).vectorize(lu).unroll(n).unroll(m);
    codes.update().reorder(rxo, lane, u, n, m).fuse(lane, u, lu).atomic().vectorize(rxo).vectorize(lu).unroll(n).unroll(m);
    quads.compute_at(acc, ryo).reorder_storage(q, lane, u, n, m).bound_storage(u, ts.interleave);
    quads.reorder(q, lane, u, n, m).fuse(q, lane, qlv).vectorize(qlv).unroll(u).unroll(n).unroll(m);
    quads.update().reorder(rxi, q, lane, rxh, u, n, m).fuse(q, lane, qlv).atomic().vectorize(rxi).vectorize(qlv).unroll(rxh).unroll(u).unroll(n).unroll(m);
    tables(rs, {blk, codes, quads}, LoopLevel(acc, ryo));
}

// vec_dot: a constant code offset's term (derived_sums(): d.offsets, d.sub_sums,
// tq2_0's and tq1_0's -1 times the act's sub-block sums, scaled per block as
// the codes' dot) merged into the codes' int32 lanes (codes, of blk): the
// act's sub-block sums (int16) reduced to `lanes` (each that of 2 pairs of
// sub-blocks: saddlp, sadalp) and taken off the lanes before their float
// conversion, as GGML's ggml_vec_dot_tq2_0_q8_K: per block one conversion
// and multiply-add, not two and a horizontal add (tq2_0 and tq1_0 ~5% faster
// per block). The lanes' float sums reassociate (not bit for bit, within the
// check's tolerance). False (nothing changed) where the terms' block scales
// differ or the sums don't split across the lanes.
inline bool merge_sums(Func dot, const Stage &ds, Sums &d, Func blk, Func codes, int lanes, LoopLevel at, int interleave) {
    using namespace Internal;
    if (!d.offsets || !d.sub_sums || !d.sum.defined() || d.part.defined() || d.pair.defined() || !codes.defined() || codes.type() != Int(32)) return false;
    // f's update: f + scale * term
    auto scale = [](Func f) -> Expr {
        Expr v = remove_promises(remove_likelies(substitute_in_all_lets(f.update_value())));
        const Add *a = v.as<Add>();
        const Mul *m = a ? a->b.as<Mul>() : nullptr;
        return m && m->b.as<Call>() ? m->a : Expr();
    };
    Expr sw = scale(blk), ss = scale(d.blk);
    if (!sw.defined() || !ss.defined() || !equal(sw, ss)) return false;
    // dot's increment: with + k * sums (k an integer)
    Definition dd = stage_definition(dot, ds);
    std::optional<int> k;
    auto sums = [&](const Expr &e) {
        const Call *c = e.as<Call>();
        return c && c->name == d.sums.name();
    };
    Expr v = mutate_with(dd.values()[0], [&](auto *self, const Mul *op) -> Expr {
        const FloatImm *f = op->a.as<FloatImm>();
        if (sums(op->b) && f && f->value == (int)f->value) {
            k = (int)f->value;
            return make_zero(op->type);
        }
        return self->visit_base(op); }, [&](auto *self, const Sub *op) -> Expr {
        if (sums(op->b)) {
            k = -1;
            return self->mutate(op->a);
        }
        return self->visit_base(op); });
    if (!k) return false;
    const std::vector<ReductionVariable> &rv = d.sum.function().update(0).schedule().rvars();
    std::optional<int64_t> ext = rv.size() == 1 ? as_const_int(simplify(rv[0].extent)) : std::nullopt;
    if (!ext || *ext % (2 * lanes)) return false;  // (sub-block pairs per lane)
    // blk's float(codes) (a wrapper's: change_type()) inlined
    Definition bd = stage_definition(blk, blk.update());
    Inliner in;
    visit_with(bd.values()[0], [&](auto *self, const Call *c) {
        if (c->func.defined()) {
            Function f(c->func);
            const Cast *x = f.has_update_definition() ? nullptr : f.values()[0].as<Cast>();
            const Call *cc = x ? x->value.as<Call>() : nullptr;
            if (cc && cc->name == codes.name()) in.add(f);
        }
        self->visit_base(c);
    });
    Expr bv = in(bd.values()[0]);
    if (!calls_any(bv, {codes})) return false;
    // the act's sub-block t = (rk * lanes + lane) * 2 + rj
    Var lane("lane");
    RVar rt(rv[0].var), rk("rk"), rr("rr"), rl("rl"), rj("rj");
    d.sum.update().split(rt, rk, rr, 2 * lanes).split(rr, rl, rj, 2);
    Func ls = d.sum.update().rfactor(rl, lane);
    std::vector<std::string> names;
    for (Var a : codes.args()) {
        names.push_back(a.name());
    }
    bd.values()[0] = mutate_with(bv, [&](auto *self, const Call *c) -> Expr {
        if (c->name != codes.name()) return self->visit_base(c);
        std::vector<Expr> args;
        for (Var a : ls.args()) {
            args.push_back(c->args[std::find(names.begin(), names.end(), a.name()) - names.begin()]);
        }
        return Expr(c) + *k * ls(args);
    });
    dd.values()[0] = simplify(v);
    Var u("u");
    regs(ls.compute_at(at).reorder_storage(lane, u).bound_storage(u, interleave), {lane}, {u});
    ls.update().reorder(rj, lane, rk).atomic().vectorize(rj).unroll(rk);
    d = Sums();
    return true;
}

// A block's codes (codes: their dot per lane, chunks rxc of `nchunk`, each
// `per` values per lane, rxi) whose bit fields (x >> s) & m sit at a position s
// that varies with the chunk (tq2_0's 2-bit fields: s = 0, 2, 4, 6 by pairs
// of chunks): the chunks summed per position (an accumulator each: rfactor,
// c), each field read in place, x & (m << s), as long as that fits int8
// (s = 2, 4; s = 6's, 192, doesn't: shifted as before), its sum shifted
// right after (ssra; exact, every product a multiple of 2^s): a mask per
// field, not a shift and a mask (tq2_0: 8 fewer SIMD instructions per
// block). The codes' sum must be linear in the field. False (codes
// untouched) where no field's position varies so, or it isn't linear.
inline bool deferred_shifts(Func codes, RVar rxc, int nchunk, int lanes, int per, LoopLevel at, int interleave, Var n, Var m) {
    using namespace Internal;
    // the field's mask m and position s
    Expr e = remove_promises(remove_likelies(substitute_in_all_lets(codes.update_value())));
    std::vector<std::pair<Expr, uint64_t>> fields;
    auto field = [&](const Call *op, Expr &x, Expr &sh, uint64_t &mask) {
        if (!op->is_intrinsic(Call::bitwise_and)) return false;
        const Call *c = op->args[0].as<Call>();
        auto k = as_const_uint(op->args[1]);
        if (!c || !c->is_intrinsic(Call::shift_right) || !k || is_const(c->args[1])) return false;
        x = c->args[0], sh = c->args[1], mask = *k;
        return true;
    };
    visit_with(e, [&](auto *self, const Call *op) {
        Expr x, sh;
        uint64_t mask;
        if (field(op, x, sh, mask) && expr_uses_var(sh, rxc.name())) fields.emplace_back(sh, mask);
        self->visit_base(op);
    });
    if (fields.size() != 1) return false;
    // the codes' sum linear in the field (it times 2^s: the sum times 2^s):
    // only casts, shifts left by 0, products, sums between them (0: no field,
    // 1: linear, -1: not)
    std::function<int(const Expr &)> linear = [&](const Expr &x) -> int {
        Expr f, sh;
        uint64_t mask;
        auto both = [&](const Expr &a, const Expr &b, bool product) {
            int p = linear(a), q = linear(b);
            if (p < 0 || q < 0 || (product && p && q) || (!product && p != q)) return -1;
            return std::max(p, q);
        };
        if (const Call *c = x.as<Call>()) {
            if (field(c, f, sh, mask)) return 1;
            if (c->name == codes.name()) return 1;
            if (c->is_intrinsic(Call::widening_mul)) return both(c->args[0], c->args[1], true);
            if (c->is_intrinsic(Call::shift_left) && is_const_zero(c->args[1])) return linear(c->args[0]);
        } else if (const Cast *c = x.as<Cast>()) {
            if (c->type.is_int_or_uint() && c->value.type().is_int_or_uint()) return linear(c->value);
        } else if (const Mul *m = x.as<Mul>()) {
            return both(m->a, m->b, true);
        } else if (const Add *a = x.as<Add>()) {
            return both(a->a, a->b, false);
        }
        bool uses = false;
        visit_with(x, [&](auto *self, const Call *c) {
            uses |= field(c, f, sh, mask);
            self->visit_base(c);
        });
        return uses ? -1 : 0;
    };
    if (linear(e) != 1) return false;
    auto [sh, mask] = fields[0];
    std::vector<int> pos;
    for (int i = 0; i < nchunk; i++) {
        std::optional<int64_t> p;
        for (auto [l, x] : {std::pair{0, 0}, std::pair{lanes - 1, per - 1}}) {
            auto q = as_const_int(simplify(substitute({{rxc.name(), Expr(i)}, {"lane", Expr(l)}, {"rxi", Expr(x)}}, sh)));
            if (!q || (p && *p != *q)) return false;
            p = q;
        }
        pos.push_back((int)*p);
    }
    // the positions by chunk: s(rxc) = s_c, c = rxc / runs mod classes (the
    // fewest)
    int runs = 0, classes = 0;
    for (int g = 2; g <= nchunk && !runs; g++) {
        for (int r = 1; r * g <= nchunk && !runs; r++) {
            if (nchunk % (r * g)) continue;
            bool ok = true;
            for (int i = 0; i < nchunk; i++) {
                ok &= pos[i] == pos[(i / r % g) * r];
            }
            if (ok) runs = r, classes = g;
        }
    }
    if (!runs) return false;
    std::vector<int> shift(classes);
    for (int g = 0; g < classes; g++) {
        int p = pos[g * runs];
        shift[g] = p > 0 && (mask << p) <= 127 ? p : 0;
    }
    if (std::all_of(shift.begin(), shift.end(), [](int x) { return x == 0; })) return false;
    RVar rxa("rxa"), rcp("rcp"), rco("rco"), rcg("rcg");
    Var c("c"), lane("lane"), u("u");
    codes.update().split(rxc, rxa, rcp, runs).split(rxa, rco, rcg, classes);
    Func ch = codes.update().rfactor(rcg, c);
    Definition cd = stage_definition(ch, ch.update());
    cd.values()[0] = mutate_with(cd.values()[0], [&](auto *self, const Call *op) -> Expr {
        Expr x, sh;
        uint64_t mask;
        if (!field(op, x, sh, mask)) return self->visit_base(op);
        std::vector<Expr> d;
        for (int g = 0; g < classes; g++) {
            d.push_back(make_const(sh.type(), shift[g]));
        }
        Expr dc = mux(c, d);
        return (x >> (sh - dc)) & (make_const(x.type(), mask) << dc);
    });
    Definition md = stage_definition(codes, codes.update());
    md.values()[0] = mutate_with(md.values()[0], [&](auto *self, const Call *op) -> Expr {
        if (op->name != ch.name()) return self->visit_base(op);
        std::vector<Expr> parts;
        for (int g = 0; g < classes; g++) {
            parts.push_back(Expr(op) >> shift[g]);
        }
        return mux(op->args.back(), parts);
    });
    ch.compute_at(at).reorder_storage(lane, u, c, n, m).bound_storage(u, interleave);
    regs(ch, {lane}, {c, u, n, m});
    ch.update().reorder(RVar("rxi"), lane, c, rcp, rco).atomic().vectorize(RVar("rxi")).unroll(rcp).unroll(rco);
    codes.update().unroll(rcg);
    return true;
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
// decode of q4_K's 6-bit scales and mins). `merge` (vec_dot): a constant code
// offset's sums merged into the codes' lanes (merge_sums()). `defer`
// (vec_dot and gemv, no sub-blocks): the codes' bit fields' shifts
// deferred to their sums (deferred_shifts()).
inline void blocked(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t, bool derived = false,
                    int sub = 0, int act_sub = 0, bool sublanes = false, bool gpr_scales = false, bool fold_offsets = true,
                    bool merge = false, bool defer = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r), ds = s;
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), u("u"), v("v"), bacc("bacc");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    int per = integer ? 4 : 1, vec = t.natural_vector_size<int8_t>();
    if (sub * per <= vec) sub = 0;  // a sub-block per lane step: no sum to hoist its scale from
    std::optional<int64_t> bx = Internal::as_const_int(r.x.extent());
    int nsub = sub && bx ? (int)*bx / sub : 0;
    if (nsub && integer && !sublanes && !gpr_scales && !affine(rs) && float_sub_scales(rs) && nsub * ts.interleave == vec / per) {
        blocked_sub_lanes(out, st, s, dot.args()[0], dot.args()[1], r, ts, sub, nsub, vec, per, rs);
        return;
    }
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
    if (derived && derived_sums(out, st, dot, s, r, rs, d, act_sub, sub > act_sub ? sub : 0, dkept, fold_offsets)) aff = false;
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
    if (merge && !sub) merge_sums(dot, ds, d, blk, codes, vec / per, LoopLevel(acc, ryo), ts.interleave);
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
    std::optional<int64_t> nchunk = bx ? std::optional<int64_t>(*bx / vec) : std::nullopt;
    for (Func f : {codes, rest}) {
        if (!f.defined() || sub) continue;
        if (f.name() == codes.name() && defer && nchunk && integer && deferred_shifts(codes, rxc, (int)*nchunk, vec / per, per, LoopLevel(acc, ryo), ts.interleave, n, m)) continue;
        f.update().atomic().vectorize(rxi).unroll(rxc);
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

// blocked_rows() for two-level weights (q4_K's repacked block_q4_Kx8, as
// GGML's ggml_gemv_q4_K_8x8_q8_K; q5_K's and q6_K's): per block and sub-block of `sub`
// values, the int32 dot of the codes vectorized across the record's rows and
// each piece's quads (sdot, the act's piece broadcast to the rows), the
// quads merged, times the sub-block's scales (a vector across the rows),
// summed over the sub-blocks in int32, then times the rows' and act's
// scales into f32 accumulators across the rows. An affine weight's min term
// reads the act's sub-block sums (`act_sub`: q8_K's bsums; derived_sums()),
// times the rows' mins as a vector, alongside.
inline void blocked_rows_sub(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int chunk,
                             const std::vector<ApproximationResult> &rs, int sub, int act_sub) {
    Stage s = tile_out(out, st, dot, cond, ts, r, true);
    Var n = dot.args()[0], m = dot.args()[1], q("q"), u("u"), v("v"), bacc("bacc");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rp("rp"), rq("rq"), ri("ri"), ryo("ryo"), ryi("ryi_rows");
    Sums d;
    if (!sub || (affine(rs) && !derived_sums(out, st, dot, s, r, rs, d, act_sub, sub))) throw std::invalid_argument("blocked_rows_sub: two-level weights (affine: an act with derived sums)");
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
    // Groups larger than pairs where a bit field's position varies across
    // pairs, so its shifts are constants: as many as share its bytes (q6_K's
    // 8 sub-blocks of 16 per 128 values) or all (q5_K's high bits: bit s of
    // sub-block s).
    auto bx = as_const_int(r.x.extent());
    int subs = bx ? (int)*bx / sub : 2, group = 2;
    while (group < subs && bit_position_varies(quads, v, group))
        group *= 2;
    codes.update().reorder(n, m, rxs, u).split(rxs, rxso, rxsi, group).unroll(rxsi);
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
// `per` > 1: weight blocks of `per` blocks (scale-only weights, one level):
// per weight block (w), its scale times the sum of its blocks' (win, each
// the act's scale times the codes' dot), so the weight's scale multiplies
// once per weight block, not per block.
inline void blocked_mmla(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r,
                         const std::vector<ApproximationResult> &rs, bool derived = false, int sub = 0, int act_sub = 0, bool records = false, bool act_records = false,
                         int per = 1, bool fold_offsets = true, bool gpr_scales = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r, records);
    Var n = dot.args()[0], m = dot.args()[1], nio("nio"), nii("nii"), mio("mio"), mii("mii"), u("u"), v("v"), bacc("bacc"), w("w");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rxk("rxk"), ryo("ryo"), ryi("ryi"), ryw("ryw"), ryp("ryp");
    bool aff = affine(rs);
    if (per > 1 && (aff || derived || sub || records)) throw std::invalid_argument("blocked_mmla: weight blocks of several blocks, scale-only weights");
    Sums d;
    // records: the sub-blocks' decoded scales (and mins) their own stage
    std::vector<Func> kept = records && sub ? scale_decoders(rs) : std::vector<Func>{};
    // gpr_scales: the plain weights' sub-block scales too, decoded per block
    // per row in GPRs (below), not per sub-block per row pair
    bool swar = gpr_scales && sub && !records;
    if (swar) kept = scale_decoders(rs);
    if (derived && derived_sums(out, st, dot, s, r, rs, d, act_sub, records ? sub : 0, kept, fold_offsets)) aff = false;
    // With the act's records: if, within the period over which the bit
    // fields' positions repeat (bit_period: q6_K's 128 values), a bit of the
    // sub-block's index that moves no field (q6_K's bit 0) lies beneath ones
    // that do, r.x split by that period first, so that bit can be a loop
    // inside the period's (the others unrolled): x then the period's multiple
    // plus the loop's, which the simplifier reduces to dense loads (split in
    // sub-blocks first, ((p * 8 + b) * 16 + c) % 128 stays: gathers).
    auto bx = as_const_int(r.x.extent());
    RVar rxp("rxp");
    Var vp("vp");
    int period = 0;
    if (sub && act_records && bx) {
        std::vector<Func> dec;
        for (Func f : decoders(rs)) {
            if (std::none_of(kept.begin(), kept.end(), [&](const Func &k) { return k.name() == f.name(); })) dec.push_back(f);
        }
        period = bit_period(dot, r.x, sub, (int)*bx / sub, dec);
    }
    if (period) {
        RVar rxx("rxx");
        s.split(r.x, rxp, rxx, period).split(rxx, rxs, rxc, sub).split(rxc, rxc, rxk, 8);
    } else {
        sub ? s.split(r.x, rxs, rxc, sub).split(rxc, rxc, rxk, 8) : s.split(r.x, rxc, rxk, 8);
    }
    Func wblk, win;
    Stage bs = s;  // the stage over the blocks (ry; per weight block, ryp)
    if (per > 1) {
        s.split(ry, ryw, ryp, per);
        wblk = s.rfactor(ryw, w);
        wblk.update().eager_inline(decoders(rs, all_but({tag::codes}), 0, 1));
        win = wblk.update().hoist_invariants()[0];
        if (win.name() == wblk.name()) throw std::invalid_argument("blocked_mmla: no weight scale per weight block in " + wblk.name());
        bs = win.update();
    }
    Func blk = bs.rfactor(per > 1 ? ryp : ry, u);
    bool fsub = sub && !records && float_sub_scales(rs);
    Func rest, mid, codes = block_dot(blk, rs, !fsub, rest, mid, aff, aff ? std::vector<Func>{} : kept);
    // per sub-block (v): its scale times its codes' dot (inner), as blocked()
    Func part, inner;
    if (sub) {
        part = period ? codes.update().rfactor({{rxs, v}, {rxp, vp}}) : codes.update().rfactor(rxs, v);
        inner = part.update().hoist_invariants()[0];
        if (fsub) inner = inner.change_type(Int(32));
    }
    s.split(per > 1 ? ryw : ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor(ryi, bacc);
    s.vectorize(n, 2, TailStrategy::GuardWithIf).unroll(n).unroll(m);
    for (Func f : {acc, blk, codes, mid, part, inner}) {
        if (!f.defined()) continue;
        if (per > 1 && f.name() != acc.name()) {  // (n, m, w, u): per block of win's
            sub_tiles(sub_tile_storage(f, n, m, {u, w}), n, m, {nii, mii, nio, mio, u, w}).unroll(u).unroll(w);
            f.compute_at(win, ryp).bound_storage(u, 1).bound_storage(w, 1);
            continue;
        }
        Var x = f.args()[2];  // x: bacc or u
        std::vector<Var> outer{x};
        if (f.dimensions() > 3) outer.insert(outer.begin(), v);
        if (f.dimensions() > 4) outer.insert(outer.begin() + 1, vp);
        sub_tiles(sub_tile_storage(f, n, m, outer), n, m, {nii, mii, nio, mio, x}).unroll(x);
        // per block (per 2 rows, tall tiles keep the weight's codes live and spill)
        if (x.name() == u.name()) f.compute_at(acc, ryo).bound_storage(u, ts.interleave);
    }
    for (Func f : {wblk, win}) {  // per weight block
        if (!f.defined()) continue;
        sub_tiles(sub_tile_storage(f, n, m, {w}), n, m, {nii, mii, nio, mio, w}).unroll(w);
        f.compute_at(acc, ryo).bound_storage(w, ts.interleave);
        sub_tiles(f.update(), n, m, f.name() == win.name() ? std::vector<VarOrRVar>{nii, mii, nio, mio, ryp, w} : std::vector<VarOrRVar>{nii, mii, nio, mio, w}).unroll(w);
    }
    if (sub) {  // per block and sub-block: the tile's sub-block dots, then their scales
        // With the act's records, a loop over groups of sub-blocks whose bit
        // fields' positions repeat (constant shifts), as blocked_rows_sub's
        // (q4_K's pairs, q6_K's 128 values; fully unrolled, the codes' loads
        // are scheduled early and spill: 4-7% slower), each unrolled, those
        // reading the same bytes adjacent (sharing_order: q6_K's 0, 4, 2, 6).
        // Per period, its bits that move no field loops inside the unrolled
        // ones (q6_K's: 4 sub-blocks unrolled per loop iteration, pairs
        // sharing the low bits' bytes, all 4 the high bits'; no stack
        // round trips of the raw bytes 8 unrolled took, 865 -> 2 x 373
        // instructions per 128 smmla).
        Stage cu = codes.update();
        int subs = bx ? (int)*bx / sub : 2;
        if (period) subs = period / sub;
        int group = act_records && !period ? 2 : subs;
        while (group < subs && bit_position_varies(inner, v, group))
            group *= 2;
        RVar rxso("rxso"), rxsi("rxsi"), g = rxs, at_sub;
        if (group < subs) {
            cu.split(rxs, rxso, rxsi, group);
            g = rxsi;
        }
        std::vector<VarOrRVar> order{nii, mii, nio, mio};
        std::vector<int> counts, bits = act_records ? sharing_order(inner, v, group, &counts) : std::vector<int>{};
        size_t loops = 0;
        if (std::is_sorted(bits.begin(), bits.end())) {
            order.push_back(at_sub = g);
        } else {  // g's bits, each an RVar (the lowest first), in that order
            std::vector<RVar> bv;
            for (size_t b = 0; b + 1 < bits.size(); b++) {
                RVar hi("rxsh" + std::to_string(b)), lo("rxsb" + std::to_string(b));
                cu.split(g, hi, lo, 2);
                bv.push_back(lo);
                g = hi;
            }
            bv.push_back(g);
            for (int b : bits) {
                if (!period || counts[b]) order.push_back(bv[b]);
            }
            for (int b : bits) {
                if (period && !counts[b]) order.push_back(bv[b]), loops++;
            }
            at_sub = bv[bits[0]];
        }
        size_t unrolled = order.size() - loops;
        if (group < subs) order.push_back(rxso);
        if (period) order.push_back(rxp);
        order.push_back(u);
        sub_tiles(cu, n, m, order).unroll(u);
        for (size_t i = 4; i < unrolled; i++) {
            cu.unroll(order[i]);
        }
        if (records) {
            // the scales' factor one 16-lane vector per sub-block (2 x 2
            // sub-tiles of the tile's columns): one shuffle of the decoded
            // scales, not one per sub-tile
            Var no("no"), ni("ni"), mo("mo"), mi("mi"), nm("nm"), nmo("nmo");
            part.update().split(n, no, ni, 2, TailStrategy::RoundUp).split(m, mo, mi, 2, TailStrategy::RoundUp).reorder({ni, mi, no, mo, v, u}).fuse(ni, mi, nm).fuse(nm, no, nmo).vectorize(nmo).unroll(mo).unroll(v).unroll(u);
            if (period) part.update().unroll(vp);
        } else {
            sub_tiles(part.update(), n, m, {nii, mii, nio, mio, v, u}).unroll(v).unroll(u);
        }
        part.compute_at(codes, at_sub);
        inner.compute_at(codes, at_sub);
        // the sub-block's scales, decoded once across the record's rows (per
        // period, its sub-blocks', the rows innermost: dense in a looped
        // sub-block, not gathered)
        for (Func k : swar ? std::vector<Func>{} : kept) {
            if (!calls_any(part.function().update(0).values()[0], {k})) continue;
            Func ks = k.in(part);
            if (period) ks.reorder_storage(ks.args()[2], ks.args()[0], ks.args()[1]);
            ks.compute_at(codes, period ? rxp : at_sub).vectorize(ks.args()[2], ts.nt);
            ks.unroll(ks.args()[0]).unroll(ks.args()[1]);
        }
    }
    acc.compute_at(tile_at(out, st));
    sub_tiles(acc.update(), n, m, {nii, mii, nio, mio, bacc, ryo}).unroll(bacc);
    std::vector<VarOrRVar> wv = per > 1 ? std::vector<VarOrRVar>{w} : std::vector<VarOrRVar>{}, bo{nii, mii, nio, mio, u}, co{rxk, nii, mii, rxc, nio, mio, u};
    bo.insert(bo.end(), wv.begin(), wv.end());
    co.insert(co.end(), wv.begin(), wv.end());
    for (Func f : {blk, mid}) {
        if (f.defined()) sub_tiles(f.update(), n, m, bo).unroll(u);
    }
    sub_tiles((sub ? inner : codes).update(), n, m, co).unroll(rxc).unroll(u).atomic().vectorize(rxk);
    if (rest.defined()) {  // the act's block sum, once per act column (n vectorized)
        regs(rest.compute_at(acc, ryo).bound_storage(u, ts.interleave), {n}, {m, u});
        rest.update().reorder(rxk, rxc, n, m, u).atomic().vectorize(rxk).vectorize(rxc);
    }
    schedule_sums(d, out, st, acc, n, m, ts, r, true, 16, records, act_records);
    // a tile-local wrapper (k is shared with the other tiles), computed per
    // block: four scales per word, the rows unrolled (with SIMD across the
    // rows instead, or SIMD only: 3-4% slower)
    for (Func k0 : swar ? kept : std::vector<Func>{}) {
        std::vector<Func> callers;
        for (Func g : {part, inner, codes, blk, mid, rest, d.sums, d.blk, d.sum, d.part, d.pair}) {
            if (!g.defined()) continue;
            bool c = false;
            for (int i = -1; i < g.num_update_definitions(); i++) {
                const auto &vals = i < 0 ? g.function().values() : g.function().update(i).values();
                c = c || std::any_of(vals.begin(), vals.end(), [&](const Expr &e) { return calls_any(e, {k0}); });
            }
            if (c && std::none_of(callers.begin(), callers.end(), [&](const Func &h) { return h.name() == g.name(); })) callers.push_back(g);
        }
        if (callers.empty()) continue;
        Func k = k0.in(callers);
        Var kw("kw"), kb("kb");
        k.compute_at(acc, ryo)
            .split(k.args()[0], kw, kb, 4)
            .unroll(kw)
            .unroll(k.args()[1])
            .vectorize(kb, 4, VectorizeIn::GPR)
            .unroll(k.args()[2]);
    }
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

// Where a weight's constant code offsets fold into the act's sub-block sums
// (derived_sums()): vec_dot and every mul_mat tile, vec_dot and the gemv
// tiles (GGML's repacked i8mm q6_K gemm subtracts its offset per value: the
// sums' products with its sub-block scales cost the gemm tiles more), or
// nowhere (a per-format parameter).
enum class Fold {
    None,
    Gemv,
    All,
};

// The N = M = 1 schedule (GGML's vec_dot). Activations encoded inside the
// pipeline (`staged`) are encoded once, up front. `interleave` blocks per
// iteration (blocked weights; a per-format parameter, formats.cmake).
inline void vec_dot(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t, const Approximation &acc = Approximation(), int interleave = 2,
                    bool sublanes = false, bool gpr_scales = false, Fold fold_offsets = Fold::All) {
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
    blocked(out, 0, dots[0], Expr(), {1, 1, interleave}, r, fmt.act_block == fmt.block, rs, t, true, fmt.sub, fmt.act_sub, sublanes, gpr_scales, fold_offsets != Fold::None, true, true);
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
// block (single rows for N = 1, for weights decoded through tables, or
// without `row_pairs`), with its `interleave`, `sublanes` and `gpr_scales`
// (per-format parameters). Edge
// tiles shift inwards: any N, M at least the tile's. With act records of
// r = fmt.act_rows > 1 rows, out's first update is the rows in whole records
// (dots[1]) on gemm tiles starting on a record, its second the others
// (dots[0], < r rows) on gemv tiles.
inline void mul_mat(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t, const Approximation &acc = Approximation(), int interleave = 2,
                    bool sublanes = false, bool gpr_scales = false, Fold fold_offsets = Fold::All, bool row_pairs = true) {
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
    bool fold_gemm = fold_offsets == Fold::All, fold_gemv = fold_offsets != Fold::None;
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
    bool recs = integer && fmt.rows > 1 && fmt.sub, rmmla = mmla_ok && recs;
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
            lanes ? blocked_rows(out, whole, dr, c, ts, r, fmt.chunk, rs, fmt.act_rows) : blocked_mmla(out, whole, dr, c, ts, r, rs, false, 0, 0, false, false, fmt.sub ? 1 : fmt.per);
        }
    } else if (rmmla) {
        // whole records of rows (from 0 mod their rows: their addresses dense)
        // (dr's act is the last result: its derived ports, rs[1]'s for derived_sums)
        std::vector<ApproximationResult> rr = rs;
        if (whole) std::swap(rr[1], rr.back());
        blocked_mmla(out, whole, dr, whole ? Expr() : fits(rm, M), rm, r, rr, true, fmt.sub, fmt.act_sub, true, whole, 1, fold_gemm);
    } else if (mmla_ok) {
        blocked_mmla(out, whole, dr, fits(mmla8, M), mmla8, r, rs, true, fmt.sub, fmt.act_sub, false, false, 1, fold_gemm, gpr_scales);
    }
    // (records without i8mm: their gemv per column; blocked()'s tiles gather)
    if (!(recs && !whole) && (!whole || !tiled)) blocked(out, whole, dr, whole ? Expr() : fits(gemm, M), gemm, r, integer, rs, t, !whole, fmt.sub, fmt.act_sub, false, false, fold_gemm);
    // The rows past Mr (fewer than a record's): no tasks when there are none.
    Expr rest = whole ? M % fmt.act_rows != 0 : Expr();
    if (integer && fmt.rows > 1) {  // N is a multiple of the records' rows
        Tiles rt{fmt.rows, 1, 2, 16, 0, 0, rest};
        // (a two-level record, 8 rows of q4_K: 1152 bytes, one per iteration)
        if (fmt.sub) {
            rt.interleave = 1;
            blocked_rows_sub(out, 2 * whole, dots[0], Expr(), rt, r, fmt.chunk, rs, fmt.sub, fmt.act_sub);
            return;
        }
        // Codes decoded from their field (packed or mapped, not q8_0's bytes):
        // two records per tile where N allows, each act piece's broadcast
        // serving both (on M3, the gemv of mxfp4.4x4 1.08-1.1x, q4_0.i4.4x8
        // 1.04-1.1x, q1_0.4x1 1.02-1.05x; q8_0's, a load per piece, 0.85x at
        // K = 14336: two weight streams)
        std::vector<Func> cd = decoders(rs, only({tag::codes}), 0, 1);
        if (!cd.empty() && cd.front().type() != cd.back().type()) {
            Tiles r2 = rt;
            r2.nt = 2 * fmt.rows;
            blocked_rows(out, 2 * whole, dots[0], N % r2.nt == 0, r2, r, fmt.chunk, rs);
        }
        blocked_rows(out, 2 * whole, dots[0], Expr(), rt, r, fmt.chunk, rs);
        return;
    }
    pairs.busy = rows.busy = rest;
    pairs.interleave = rows.interleave = interleave;
    // A staged table's decode, per row pair, outweighs sharing the act's loads
    // (`row_pairs`: a per-format parameter)
    if (row_pairs && staged_tables(rs).empty()) blocked(out, 2 * whole, dots[0], fits(pairs, 1), pairs, r, integer, rs, t, true, fmt.sub, fmt.act_sub, sublanes, gpr_scales, fold_gemv, false, true);
    blocked(out, 2 * whole, dots[0], Expr(), rows, r, integer, rs, t, true, fmt.sub, fmt.act_sub, sublanes, gpr_scales, fold_gemv, false, true);
}

}  // namespace ggml
