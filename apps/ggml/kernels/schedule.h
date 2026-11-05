#pragma once

// Schedules of the mul_mat algorithm (kernels/matmul.cpp). They see its
// structure only: the reduction r (r.x within a block, r.y over the blocks),
// the values per record of each operand, and the operands'
// ApproximationResults.
#include "Halide.h"
#include "schemes/common.h"
#include "schemes/tags.h"

#include <functional>
#include <map>
#include <set>

namespace ggml {

using namespace Halide;

// Is a Func named `name` among fs?
inline bool has(const std::vector<Func> &fs, const std::string &name) {
    return std::any_of(fs.begin(), fs.end(), [&](const Func &f) { return f.name() == name; });
}

// The Funcs f's definitions call.
inline std::vector<Func> callees(Func f) {
    std::vector<Func> fs;
    auto add = [&](const Expr &v) {
        Internal::visit_with(v, [&](auto *self, const Internal::Call *c) {
            if (c->call_type == Internal::Call::Halide &&
                std::none_of(fs.begin(), fs.end(), [&](const Func &h) { return h.name() == c->name; })) {
                fs.push_back(Func(Internal::Function(c->func)));
            }
            self->visit_base(c);
        });
    };
    for (const Expr &v : f.function().definition().values()) {
        add(v);
    }
    return fs;
}

// The Tabulate (schemes/common.h) whose decode in r defines f: its lut (a
// constant of (element, index)) if `lut`, else the decode reading it (with
// `precomputed`, only if it reads it precomputed: Func::precompute()).
inline const Tabulate *tabulate(const ApproximationResult &r, const Func &f, bool lut, bool precomputed = false) {
    const ApproximationTraceNode *n = r.defining_call(f);
    const Tabulate *t = n ? n->stage.target<Tabulate>() : nullptr;
    if (!t || !has(lut ? n->intermediates : n->ports, f.name())) return nullptr;
    return !precomputed || n->intermediates[0].function().schedule().precomputed() ? t : nullptr;
}
inline const Tabulate *tabulate(const std::vector<ApproximationResult> &rs, const Func &f, bool lut) {
    for (const ApproximationResult &r : rs) {
        if (const Tabulate *t = tabulate(r, f, lut)) return t;
    }
    return nullptr;
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

// The decode Funcs of rs[first, last) of the components `pick` selects; with
// `tables`, those reading a precomputed lut (luts()) instead.
inline std::vector<Func> decoders(const std::vector<ApproximationResult> &rs, const Pick &pick = all_but({}), size_t first = 0, size_t last = SIZE_MAX, bool tables = false) {
    std::vector<Func> fs;
    for (size_t i = first; i < std::min(last, rs.size()); i++) {
        for (Func f : rs[i].decode_funcs()) {
            if (pick(rs[i].tags_of(f)) && tables == (tabulate(rs[i], f, false, true) != nullptr)) fs.push_back(f);
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

// fs but those among `minus`.
inline std::vector<Func> without(const std::vector<Func> &fs, const std::vector<Func> &minus) {
    std::vector<Func> r;
    for (const Func &f : fs) {
        if (!has(minus, f.name())) r.push_back(f);
    }
    return r;
}

// Does a definition of g call f?
inline bool calls(const Func &g, const Func &f) {
    std::vector<Expr> vals = g.function().definition().values();
    for (const Internal::Definition &u : g.function().updates()) {
        vals.insert(vals.end(), u.values().begin(), u.values().end());
    }
    return std::any_of(vals.begin(), vals.end(), [&](const Expr &e) { return calls_any(e, {f}); });
}

// The defined Funcs of gs (each once) that call f.
inline std::vector<Func> callers(const Func &f, const std::vector<Func> &gs) {
    std::vector<Func> cs;
    for (const Func &g : gs) {
        if (g.defined() && !has(cs, g.name()) && calls(g, f)) cs.push_back(g);
    }
    return cs;
}

// Per bit of the index of r.x's sub-blocks of `sub` values (a power of 2),
// how many of dot's loads varying with r.x's bits below `low` (0: with any)
// it leaves in place: sub-blocks differing only in it read the same bytes
// (sub = 1: q4_0's bit 4, a byte's two nibbles; q5_K's sub-blocks: qh's bits).
inline std::vector<int> shared_bits(Func dot, const RDom &r, int sub = 1, int low = 0) {
    auto bx = Internal::as_const_int(r.x.extent());
    int bits = 0, s = 0;
    while (bx && (1 << bits) < *bx)
        bits++;
    while ((1 << s) < sub)
        s++;
    std::vector<int> count(std::max(bits - s, 0), 0);
    for (uint64_t m : Internal::load_invariant_bits(dot.function().update(0), r.x.name(), bits)) {
        for (int b = s; b < bits; b++) {
            count[b - s] += (~m & ((1 << (low ? low : bits)) - 1)) && (m >> b & 1);
        }
    }
    return count;
}

// The sub-blocks per group, at least g, past which (up to subs) no load is
// shared (count: shared_bits'): a group's bit fields at constant positions.
inline int sharing_group(const std::vector<int> &count, int g, int subs) {
    int top = 0;
    for (int b = 0; b < (int)count.size() && (1 << b) < subs; b++) {
        if (count[b]) top = 2 << b;
    }
    return std::max(g, std::min(top, subs));
}

// The bits of a sub-block's index in a group of g, innermost first: those
// more loads share first (ties: the lower; count: shared_bits'). Sub-blocks
// differing in such a bit read the same bytes, so iterated innermost their
// loads are reused.
inline std::vector<int> sharing_order(const std::vector<int> &count, int g) {
    std::vector<int> order;
    for (int b = 0; (1 << b) < g; b++) {
        order.push_back(b);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return count[a] > count[b]; });
    return order;
}

// The runtime's thread count.
inline Expr threads() {
    return max(Internal::Call::make(Int(32), "halide_get_num_threads", {}, Internal::Call::PureExtern), 1);
}

// Of the candidate splits ps (each `tasks` tasks of `size` work, plus
// `overhead` per task), the one whose busiest thread does the least; on
// ties, the earliest.
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
// weight's values per block (1: floats), rows per record (interleaved in
// pieces of `chunk` codes) and whether it is rowwise; the act codes' values
// per block (the weight's: an integer dot) and rows per record; two-level
// formats' values per sub-block (0: one level).
struct Formats {
    int block = 1, rows = 1, chunk = 0, act_block = 1, act_rows = 1;
    bool rowwise = false;
    int sub = 0, act_sub = 0;
    int per = 1;  // blocks per weight block (q1_0's 128 values on q8_0 acts: 4)
    // the weight's decoded values approximated again (a compute format, e.g.
    // q4_0:f16): by rs.back()
    bool wcompute = false;
    Expr row_bytes;  // a weight row's (record row's) bytes, if known
};

// Where a weight's constant code offsets fold into the act's sub-block sums
// (derived_sums()): everywhere, vec_dot and gemv only (GGML's i8mm q6_K gemm
// subtracts per value), or nowhere (a per-format parameter).
enum class Fold {
    None,
    Gemv,
    All,
};

// The per-format schedule parameters (formats.cmake; matmul.cpp's
// GeneratorParams document them).
struct Knobs {
    int interleave = 2;
    bool sublanes = false, gpr_scales = false;
    Fold fold = Fold::All;
    bool row_pairs = true, index_words = false;
};

// The tile shape of one mul_mat schedule: nt x mt outputs share each decoded
// weight row and act column (edge tiles shift inwards; M tails in out's
// updates are guarded); `interleave` blocks per iteration; parallel tasks of
// `rows` x `cols` outputs (rows 0: serial; cols 0: all of M), with `balance`:
// fewer rows where that balances the threads; with `busy`: parallel only
// where it holds.
struct Tiles {
    int nt = 1, mt = 1, interleave = 2, rows = 0, cols = 0;
    bool balance = false;
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
    Stage o = st ? out.update(st - 1) : Stage(out), s = o.specialize(cond);
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
        if (ts.balance) {
            // The tasks' tiles in n: up to rows / nt (on ties, the largest); work
            // in multiply-adds
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
        s.specialize(ts.busy).parallel(ts.cols ? t : VarOrRVar(nc));
    }
    return dot.update().specialize(cond);
}

// How a kernel reads its luts (Tabulate's): one that reads a Buffer (a
// grid's points) is precomputed on every target; one of 16 bytes or fewer
// (a codebook) is inlined on CPUs (a register table lookup: tbl, pshufb) and
// staged in threadgroup memory on GPUs (metal_luts()); the rest (bit fields'
// decodes) are precomputed with `tables` (CPUs: the decode leaves the SIMD
// units; q5_0 vec_dot 0.80x -> 1.01x GGML), else inlined. Call before any
// other directive on rs's decode.
inline bool register_lut(const std::vector<ApproximationResult> &rs, const Func &lut) {
    const Tabulate *t = tabulate(rs, lut, true);
    return t && t->per * t->entries * lut.type().bytes() <= 16;
}

inline void luts(const std::vector<ApproximationResult> &rs, bool tables) {
    std::set<std::string> seen;
    for (const ApproximationResult &r : rs) {
        for (Func f : r.decode_funcs()) {
            if (!tabulate(r, f, true) || !seen.insert(f.name()).second) continue;
            bool data = false;
            for (const Expr &v : f.function().definition().values()) {
                Internal::visit_with(v, [&](auto *self, const Internal::Call *c) {
                    data |= c->call_type == Internal::Call::Image;
                    self->visit_base(c);
                });
            }
            if (data || (tables && !register_lut(rs, f))) f.precompute();
        }
    }
}

// The table decoders that tables() stages: not tables of one value per index
// (nvfp4's scales), each a scalar load where it is used.
inline std::vector<Func> staged_tables(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> fs;
    for (Func f : decoders(rs, all_but({}), 0, SIZE_MAX, true)) {
        if (tabulate(rs, f, false)->per != 1) fs.push_back(f);
    }
    return fs;
}

// Whether f reads an array's (unsigned integer) elements as they are, by
// its first argument: a struct field's (not a scalar field, as q5_0's qh)
// or a buffer's.
inline bool plain_read(Func f) {
    if (f.dimensions() == 0 || !f.type().is_uint() || f.function().has_update_definition()) return false;
    const Internal::Call *read = f.function().definition().values()[0].as<Internal::Call>();
    if (!read || read->args.empty()) return false;
    const std::string k = f.args()[0].name();
    if (read->is_intrinsic(Internal::Call::struct_field_read)) return Internal::expr_uses_var(read->args.back(), k);
    return (read->call_type == Internal::Call::Halide || read->call_type == Internal::Call::Image) &&
           Internal::expr_uses_var(read->args[0], k);
}

// The elements of the struct field array f reads (q5's qh: 4), 0 if f
// reads none.
inline int array_extent(Func f) {
    if (f.function().has_update_definition()) return 0;
    const Internal::Call *read = f.function().definition().values()[0].as<Internal::Call>();
    if (!read || !read->is_intrinsic(Internal::Call::struct_field_read)) return 0;
    const StructTypeInfo *s = read->args[0].type().struct_type();
    auto i = Internal::as_const_int(read->args[1]);
    return s && i ? s->fields[*i].array_extent.value_or(0) : 0;
}

// A table decode g's indices, if read as is (iq3_xxs's grid index bytes),
// else, if `nested`, the bytes read as is that they are computed from (iq3_s's
// qs): staged at `at`, a word per load, in GPRs (MemoryType::GPR), each index
// a shift and a mask; in place, each is a load (iq3_xxs gemv 1.16-1.27x).
inline void staged_indices(Func g, LoopLevel at, const Target &t, bool nested) {
    std::vector<Func> idx;
    for (Func f : callees(g)) {
        if (!f.function().schedule().precomputed()) idx.push_back(f);  // (not its table)
    }
    if (idx.size() != 1) return;
    std::vector<std::pair<Func, Func>> staged;  // (Func read as is, its caller)
    if (plain_read(idx[0])) {
        staged.emplace_back(idx[0], g);
    } else if (nested) {
        // (g's own copy: the index is shared by the tables' copies for
        // other consumers, e.g. mul_mat's other tiles)
        std::vector<Func> fs;
        for (Func f : callees(idx[0])) {
            // (bytes, a word of them per load; not iq3_xxs's u32 sign
            // words, two to a word: no fewer loads)
            if (plain_read(f) && 4 * f.type().bits() <= t.bits) fs.push_back(f);
        }
        if (!fs.empty()) {
            Func own = idx[0].clone_in(g);
            for (Func f : fs) {
                staged.emplace_back(f, own);
            }
        }
    }
    for (auto [f, caller] : staged) {
        // A struct field array narrower than a GPR (q5's qh) is a word of its
        // own, loaded at its offset: a GPR-wide word would shift inwards past
        // the buffer's start for the first block.
        int lanes = t.bits / f.type().bits();
        if (int n = array_extent(f); n > 0 && n < lanes) {
            lanes = 1;
            while (lanes < n) {
                lanes *= 2;
            }
        }
        Func w = f.in(caller);
        Var k = w.args()[0], ko("ko"), ki("ki");
        w.compute_at(at).store_in(MemoryType::GPR).split(k, ko, ki, lanes).unroll(ko).vectorize(ki);
        for (size_t a = 1; a < w.args().size(); a++) {
            w.unroll(w.args()[a]);
        }
    }
}

// The table decodes at `at` (per block group), a copy for each's calls from
// `consumers`, split by the table's rows (a row read is a dense load).
inline void tables(const std::vector<ApproximationResult> &rs, const std::vector<Func> &consumers, LoopLevel at, const Target &t, bool nested = false) {
    for (Func f : staged_tables(rs)) {
        Func g = f.clone_in_callers(consumers);
        if (!g.defined()) continue;
        Var j = g.args()[0], jo("jo"), ji("ji");
        g.compute_at(at).split(j, jo, ji, tabulate(rs, f, false)->per).vectorize(ji).unroll(jo);
        for (size_t k = 1; k < g.args().size(); k++) {
            g.unroll(g.args()[k]);
        }
        staged_indices(g, at, t, nested);
    }
}

// A term of the rest (e.g. its block sum) reads the first of `ports` whose
// definition it matches (approximate_by(DerivedPort)) in `consumer`: false if
// none does. A narrower port (q8_K's int16 bsums) replaces the term retyped
// to it (change_type proves the narrower sum exact).
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

// The offset decoders and the weight's code decoders that add their values
// (q6_K's codes), if the codes the dot reads are among them (not iq2_s's),
// else none. Eager-inlined, each constant offset reaches the widening cast of
// the codes' sum, which hoist_invariants() then splits where it is exact.
inline std::vector<Func> offset_path(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> codes = decoders(rs, only({tag::codes}), 0, 1), path = offset_decoders(rs);
    for (bool grew = !path.empty(); grew;) {
        grew = false;
        for (Func f : codes) {
            if (has(path, f.name()) || f.function().has_update_definition()) continue;
            std::vector<std::pair<Expr, int>> terms;
            int64_t c = 0;
            sum_terms(f.function().definition().values()[0], 1, terms, c);
            if (std::any_of(terms.begin(), terms.end(), [&](const auto &t) {
                    Expr e = t.first;
                    while (const auto *k = e.as<Internal::Cast>()) {
                        e = k->value;
                    }
                    const auto *call = e.as<Internal::Call>();
                    return call && has(path, call->name);
                })) {
                path.push_back(f);
                grew = true;
            }
        }
    }
    // the codes the dot reads
    for (Func f : rs[0].decoded_components(tag::codes)) {
        if (has(path, f.name())) return path;
    }
    return {};
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

// Has the weight a float scale per sub-block (nvfp4's)?
inline bool float_sub_scales(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> sc = rs[0].decoded_components(tag::scale);
    return std::any_of(sc.begin(), sc.end(), [](const Func &f) { return f.type().is_float() && f.dimensions() == 3; });
}

// The weight's decoded per-sub-block scales (and mins): its scale decoders
// with the values' dimensions that no other scale decoder reads.
inline std::vector<Func> scale_decoders(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> sc = decoders(rs, only({tag::scale}), 0, 1), fs;
    for (Func f : sc) {
        if (f.dimensions() == 3 && callers(f, sc).empty()) fs.push_back(f);
    }
    return fs;
}

// blk's per-block dot (r.y rfactored to a pure var) with the decoders inlined
// and the scales hoisted: returns the dot of the codes (int32 if `integer`).
// Affine weights (`aff`) also split on the weight's codes: the code-free
// terms are returned in `rest`, and a factor common to both (the act's
// scale) hoists first into `mid`.
inline Func block_dot(Func blk, const std::vector<ApproximationResult> &rs, bool integer, Func &rest, Func &mid, bool aff, const std::vector<Func> &kept = {}) {
    Func codes;
    rest = mid = Func();
    if (!aff) {
        blk.update().eager_inline(without(decoders(rs), kept));
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

// An affine weight's code-free terms (d.sums: its min times the act), split
// off the dot's increment s by hoist_invariants, read the act's derived port
// (read_port: q8_1's s, q8_K's bsums) where one matches; per act sub-block
// (`act_sub`, d.part) or, with `sub`, per weight sub-block (d.pair: its act
// sub-blocks' port values summed). A constant code offset (offset_path():
// tq2_0, q6_K) is treated alike, as GGML subtracts offset * bsums (`fold`).
// Returns false (dot untouched) if there is nothing to read.
struct Sums {
    Func sums, blk, sum, part, pair;
    bool sub_sums = false;  // d.sum sums the act's sub-block sums (no part)
    bool offsets = false;   // a constant code offset's (offset_path())
};
inline bool derived_sums(Func out, int st, Stage &s, const RDom &r, const std::vector<ApproximationResult> &rs, Sums &d, int act_sub = 0, int sub = 0, const std::vector<Func> &kept = {}, bool fold = true) {
    d = Sums();
    if (rs.size() < 2 || rs[1].derived_ports().empty()) return false;
    std::vector<Func> dec = without(decoders(rs, all_but({tag::codes}), 0, 1), kept);
    std::vector<Func> path = fold ? offset_path(rs) : std::vector<Func>{};
    bool offsets = !path.empty();
    if (!offsets && !affine(rs)) return false;
    d.offsets = offsets;
    s.eager_inline(dec);
    if (offsets) s.eager_inline(path);
    FuncVec parts = s.hoist_invariants({decoders(rs, only({tag::codes}), 0, 1)});
    if (!parts[1].defined() && offsets) {  // no exact split (iq1's delta): no fold
        d = Sums();
        return false;
    }
    if (!parts[1].defined()) throw std::invalid_argument("derived_sums: no terms free of the codes in " + s.name());
    Func with = parts[0];
    d.sums = parts[1];
    d.blk = d.sums.update().rfactor(r.y, Var("u"));
    // a port of the act's codes alone (no scale): the act's scale hoists too
    if (act_sub) d.blk.update().eager_inline(decoders(rs, all_but({tag::codes}), 1));
    d.sum = d.blk.update().hoist_invariants()[0];
    if (act_sub) {
        // the act's codes decoded up to what its ports read: an int32 sum
        std::vector<Func> read;
        for (const DerivedPort &p : rs[1].derived_ports()) {
            read.insert(read.end(), p.from.begin(), p.from.end());
        }
        d.sum.update().eager_inline(without(decoders(rs, only({tag::codes}), 1), read));
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
        // (a constant offset's term, offset_path(): no factor per act
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

// d's sums alongside acc's update (at ryo): on gemm tiles vectorized across
// the rows (n), on gemv tiles one accumulator per interleaved block.
// `across_rows`: the sub-block sums vectorized across a record's rows too.
// `act_records`: the act also in whole records.
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
        d.blk.vectorize_all_stages(n).unroll_all_stages(u, m);
    } else {
        Func spart = d.sums.update().rfactor(ryi, v);
        d.sums.update().unroll(ryi).unroll(n).unroll(m);
        spart.compute_at(tile).unroll_all_stages(v, n, m);
        spart.update().reorder(v, n, m, ryo).compute_with(acc.update(), ryo);
        d.blk.unroll_all_stages(u, n, m);
    }
    d.blk.compute_at(blocks).bound_storage(u, ts.interleave);
    if (d.part.defined() && across_rows) {
        Var t = d.part.args().back();
        RVar rxt("rxt");
        d.sum.compute_at(blocks).vectorize_all_stages(n).unroll_all_stages(u, m);
        d.sum.update().reorder(n, rxt).unroll(rxt);
        d.part.compute_at(blocks).reorder_storage(n, t, u, m).vectorize_all_stages(n).unroll_all_stages(t, u, m);
        if (d.pair.defined()) {
            // the act's sums per weight sub-block, one vector (pairwise adds
            // of its port's), each a lane: a by-element multiply-add
            RVar rxh("rxh");
            if (act_records) {
                // whole act records (q8_K.4x8): pairwise adds of whole bsums
                // vectors (addp), not lane gathers
                Var tg("tg"), tl("tl"), sg("sg"), sl("sl");
                d.pair.compute_at(blocks).split_storage(t, sg, sl, 2).reorder_storage(sl, m, sg, n, u);
                d.pair.split(t, tg, tl, 2).reorder(tl, m, tg, n, u).vectorize(tl).vectorize(m).unroll(tg).unroll(n).unroll(u);
                d.pair.update().split(t, tg, tl, 2).reorder(rxh, tl, m, tg, n, u).atomic().vectorize(rxh).vectorize(tl).vectorize(m).unroll(tg).unroll(n).unroll(u);
            } else {
                d.pair.compute_at(blocks).reorder_storage(t, n, u, m).vectorize_all_stages(t).unroll_all_stages(n, u, m);
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
            // q6_K's int8 scales times the act's int16 sums, 8 lanes a step
            // (sshll, smull; at 16 LLVM shuffles the sign extend: q6_K's
            // vec_dot and one-thread gemv 5-6% slower on M3)
            d.part.split(t, to, ti, 8);
            d.part.update().split(t, to, ti, 8);
            d.part.vectorize_all_stages(ti).unroll_all_stages(to, u, n, m);
        } else {
            d.part.vectorize_all_stages(t).unroll_all_stages(u, n, m);
        }
        if (d.pair.defined()) {
            // the act's sums per weight sub-block (pairwise adds of its
            // port's), vectorized across them as d.part
            RVar rxh("rxh");
            d.pair.compute_at(blocks).reorder_storage(t, u, n, m).vectorize_all_stages(t).unroll_all_stages(u, n, m);
            d.pair.update().reorder(t, rxh).unroll(rxh);
        }
    } else if (d.sum.defined()) {  // no port matched: the act's block sums
        d.sum.compute_at(blocks).unroll(u).unroll(n).unroll(m);
        d.sum.update().atomic().vectorize(r.x, vec).unroll(u).unroll(n).unroll(m);
    }
}

// s's sum as a tree of partial sums (GGML's accumulators): a chain of
// rfactors, level i keeping levels[i]'s RVars as Vars (`outer` outside n and
// m). With `acc` (an Accumulate), the first `narrow` levels' sums are acc's,
// each approximated in the next level (level 0 reads its operands as stored,
// but `kept`). Each level per tile (`at`) in registers, its first `fused`
// Vars fused and vectorized. Returns the levels' Funcs.
using Level = std::vector<std::pair<RVar, Var>>;
inline std::vector<Func> sum_tree(Stage s, const std::vector<Level> &levels, LoopLevel at, Var n, Var m, RVar outer, size_t fused = 1,
                                  const Approximation &acc = Approximation(), int narrow = 0, const std::vector<Func> &kept = {}) {
    std::vector<Func> fs;
    for (const Level &l : levels) {
        fs.push_back(s.rfactor(l));
    }
    if (narrow) fs[0].update().eager_inline_all(kept);
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
        fs[i].compute_at(at).reorder_storage(vec).vectorize_all_stages(l).unroll_all_stages(unrolled);
    }
    fs[0].update().reorder(n, m, outer);
    return fs;
}

// blocked()'s two-level weights with a float scale per sub-block (nvfp4's)
// whose blocks hold `nsub` sub-blocks, fewer than an sdot's lanes, as GGML's
// NEON nvfp4 vec_dot: the lanes are the (sub-block, block) of a block group,
// each its sub-block's int32 dot (sdot pieces of `ql` quads), then its scale
// applied as one vector multiply-add per group (nvfp4 vec_dot 0.56x -> 1.03x).
inline void blocked_sub_lanes(Func out, int st, Stage s, Var n, Var m, const RDom &r, const Tiles &ts, int sub, int nsub, int vec, int per,
                              const std::vector<ApproximationResult> &rs, const Target &t) {
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
    tables(rs, {blk, codes, quads}, LoopLevel(acc, ryo), t);
}

// A block's codes whose bit fields (x >> s) & m sit at a position s that
// varies with the chunk rxc (tq2_0's 2-bit fields): the chunks summed per
// position class (rfactor, c), so hoist_invariants() hoists the shift where
// exact (the field read in place fits int8): a mask per field, then one
// shift per class sum (ssra). tq2_0 1.10-1.17x. False (codes untouched)
// where no field's position varies so.
inline bool hoisted_shifts(Func codes, RVar rxc, int nchunk, int lanes, int per, LoopLevel at, int interleave, Var n, Var m) {
    using namespace Internal;
    // the field's mask m and position s
    Expr e = remove_promises(remove_likelies(substitute_in_all_lets(codes.update_value())));
    std::vector<std::pair<Expr, uint64_t>> fields;
    visit_with(e, [&](auto *self, const Call *op) {
        const Call *c = op->is_intrinsic(Call::bitwise_and) ? op->args[0].as<Call>() : nullptr;
        auto k = c ? as_const_uint(op->args[1]) : std::nullopt;
        if (k && c->is_intrinsic(Call::shift_right) && expr_uses_var(c->args[1], rxc.name())) fields.emplace_back(c->args[1], *k);
        self->visit_base(op);
    });
    if (fields.size() != 1) return false;
    Expr sh = fields[0].first;
    uint64_t mask = fields[0].second;
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
    // some position's field read in place fits the int8 code
    // (hoist_invariants() proves which)
    if (!runs || std::none_of(pos.begin(), pos.end(), [&](int p) { return p > 0 && (mask << p) <= 127; })) return false;
    RVar rxa("rxa"), rcp("rcp"), rco("rco"), rcg("rcg");
    Var c("c"), lane("lane"), u("u");
    codes.update().split(rxc, rxa, rcp, runs).split(rxa, rco, rcg, classes);
    Func ch = codes.update().rfactor(rcg, c);
    // the position per class is invariant over the chunks given the lane's
    // and the class's ranges
    ch.bound(lane, 0, lanes).bound(c, 0, classes);
    Func in = ch.update().hoist_invariants()[0];
    for (Func f : {ch, in}) {
        f.compute_at(at).reorder_storage(lane, u, c, n, m).bound_storage(u, interleave);
        f.vectorize_all_stages(lane).unroll_all_stages(c, u, n, m);
    }
    in.update().reorder(RVar("rxi"), lane, c, rcp, rco).atomic().vectorize(RVar("rxi")).unroll(rcp).unroll(rco);
    codes.update().unroll(rcg);
    return true;
}

// The weight quantized in blocks: per block (r.y) and output a dot of the
// codes (an int32 sdot when the act is quantized alike, else f32) times the
// hoisted scales, accumulated per lane in f32 (GGML's order), on ts tiles of
// out under `cond`. `derived`: derived_sums(). Two-level weights (`sub`
// values per sub-block): the block's dot splits per sub-block (rfactor),
// whose scale hoists out of its codes' dot. `gemv` (vec_dot and the gemv
// tiles): k's sublanes (the sub-blocks across the lanes, GGML's NEON
// iq2_xs) and gpr_scales (the sub-block scales decoded in GPRs, GGML's
// q4_K), the bit fields' shifts deferred (hoisted_shifts()); else k's
// index_words.
inline void blocked(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t, bool derived = false,
                    int sub = 0, int act_sub = 0, const Knobs &k = Knobs(), bool gemv = false) {
    bool sublanes = gemv && k.sublanes, gpr_scales = gemv && k.gpr_scales, defer = gemv, index_words = !gemv && k.index_words;
    bool fold_offsets = gemv ? k.fold != Fold::None : k.fold == Fold::All;
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), u("u"), v("v"), bacc("bacc");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    int per = integer ? 4 : 1, vec = t.natural_vector_size<int8_t>();
    if (sub * per <= vec) sub = 0;  // a sub-block per lane step: no sum to hoist its scale from
    std::optional<int64_t> bx = Internal::as_const_int(r.x.extent());
    int nsub = sub && bx ? (int)*bx / sub : 0;
    if (nsub && integer && float_sub_scales(rs) && nsub * ts.interleave == vec / per) {
        blocked_sub_lanes(out, st, s, dot.args()[0], dot.args()[1], r, ts, sub, nsub, vec, per, rs, t);
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
    // scales decoded in GPRs stay their own stage
    std::vector<Func> dkept = kept, gpr;
    if (!sl && gpr_scales) {
        gpr = scale_decoders(rs);
        dkept.insert(dkept.end(), gpr.begin(), gpr.end());
    }
    if (derived && derived_sums(out, st, s, r, rs, d, act_sub, sub > act_sub ? sub : 0, dkept, fold_offsets)) aff = false;
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
    // sub-block only
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
    // rfactor put the lanes last in storage; bound_storage keeps the K
    // tail's blocks on the stack
    for (Func f : {blk, mid, codes}) {
        if (f.defined()) f.compute_at(acc, ryo).reorder_storage(lane, n, m).bound_storage(u, ts.interleave).vectorize_all_stages(lane).unroll_all_stages(u, n, m);
    }
    if (rest.defined()) {
        // the act's block sum, once per act column (n a broadcast)
        rest.compute_at(acc, ryo).reorder_storage(lane, n, m).bound_storage(u, ts.interleave).vectorize_all_stages(lane).unroll_all_stages(u, m);
        ts.nt > 1 ? rest.vectorize_all_stages(n) : rest.unroll_all_stages(n);
    }
    std::optional<int64_t> nchunk = bx ? std::optional<int64_t>(*bx / vec) : std::nullopt;
    for (Func f : {codes, rest}) {
        if (!f.defined() || sub) continue;
        if (f.name() == codes.name() && defer && nchunk && integer && hoisted_shifts(codes, rxc, (int)*nchunk, vec / per, per, LoopLevel(acc, ryo), ts.interleave, n, m)) continue;
        f.update().atomic().vectorize(rxi).unroll(rxc);
    }
    // per block and sub-block, the tile's sub-block dots (inner), then their
    // scales (part)
    std::vector<Func> readers{blk, mid, codes, rest};
    for (auto [f, part, inner] : subs) {
        readers.insert(readers.end(), {part, inner});
        f.update().reorder(lane, n, m, rxs, u).unroll(rxs);
        for (Func g : {part, inner}) {
            g.compute_at(f, rxs).reorder_storage(lane, n, m).vectorize_all_stages(lane).unroll_all_stages(v, u, n, m);
        }
        if (sl) {
            // per lane, its sub-block's sdots over its chunks (quads),
            // then summed across their lanes (pairwise adds)
            Var q("q"), ql("ql");
            Func quads = inner.update().rfactor(rxo, q);
            quads.compute_at(f, rxs).reorder_storage(q, lane, n, m).unroll_all_stages(v, u, n, m);
            quads.fuse(q, lane, ql).vectorize(ql);
            quads.update().reorder(rxi, q, lane).fuse(q, lane, ql).atomic().vectorize(rxi).vectorize(ql).unroll(rxc);
            readers.push_back(quads);
            inner.update().reorder(rxo, lane).atomic().vectorize(rxo).vectorize(lane);
        } else {
            inner.update().atomic().vectorize(rxi).unroll(rxc);
        }
    }
    schedule_sums(d, out, st, acc, n, m, ts, r, ts.nt > 2, vec);
    // gpr_scales: the scales and mins decoded per block, 4 bytes a word in a
    // GPR (q4_K vec_dot +7-12%)
    std::vector<Func> sum_readers;
    if (d.part.defined()) sum_readers.push_back(d.part);
    for (auto [f, part, inner] : subs) {
        sum_readers.push_back(part);
    }
    for (Func k : gpr) {
        Func w = k.in_callers(sum_readers);
        if (!w.defined()) continue;
        Var kw("kw"), kb("kb");
        // (all unrolled: a loop here splits the block, and LLVM spills)
        w.compute_at(acc, ryo).split(w.args()[0], kw, kb, 4).unroll(kw).vectorize(kb, 4, VectorizeIn::SWAR);
        for (size_t i = 1; i < w.args().size(); i++)
            w.unroll(w.args()[i]);
        readers.push_back(w);
    }
    for (auto [f, part, inner] : subs) {
        // the kept scales part reads: per block group, vectorized
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
    tables(rs, readers, LoopLevel(acc, ryo), t, index_words);
}

// Float weights (one value per record): per output, f32 products in vectors
// of `vec` values of r.y, ts.interleave independent accumulators (the K tail
// guarded), then summed across them.
inline void flat(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, const Target &t) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), bacc("bacc");
    RVar ry = r.y, ryo("ryo"), ryi("ryi"), ryl("ryl");
    int vec = 2 * t.natural_vector_size<float>();
    s.split(ry, ryo, ryi, vec * ts.interleave, TailStrategy::GuardWithIf).split(ryi, ryi, ryl, vec);
    sum_tree(s, {{{ryl, lane}, {ryi, bacc}}, {{ryl, lane}}}, tile_at(out, st), n, m, ryo);
    s.atomic().vectorize(ryl).unroll(n).unroll(m);
}

// flat()'s dot accumulated by `acc` (the format's Accumulate) in the order
// and roundings of GGML's NEON f16 vec_dot: a sum_tree over the bits of each
// 32-value step's lane, its first three levels approximated by acc. The K
// tail is guarded into the last step's lanes (GGML sums it in f64: not
// matched).
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
// `chunk` codes (GGML's repacked gemv): per block, int32 sdot sums vectorized
// across the record's rows, the act's piece broadcast (by-element sdot). On
// gemm tiles of more than `group` rows (KleidiAI's dotprod gemm): per act
// record, its codes staged in memory order, its scales one vector. Two-level
// weights (`sub`; GGML's ggml_gemv_q4_K_8x8_q8_K, q5_K, q6_K): per sub-block
// of `sub` values, the codes' int32 dot across the rows times the sub-block's
// scales, summed in int32, then times the block scales. An affine weight's
// min term reads the act's sub-block sums (derived_sums()).
inline void blocked_rows(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int chunk,
                         const std::vector<ApproximationResult> &rs, int group = 0, int sub = 0, int act_sub = 0) {
    std::vector<int> shared = sub ? shared_bits(dot, r, sub) : std::vector<int>{};  // (dot as defined)
    Stage s = tile_out(out, st, dot, cond, ts, r, true);
    Var n = dot.args()[0], m = dot.args()[1], q("q"), u("u"), v("v"), bacc("bacc"), mo("mog"), mi("mig");
    RVar ry = r.y, rx = r.x, rxs("rxs"), rxc("rxc"), rp("rp"), rq("rq"), ri("ri"), ryo("ryo"), ryi("ryi");
    Sums d;
    if (affine(rs) && !(sub && derived_sums(out, st, s, r, rs, d, act_sub, sub))) throw std::invalid_argument("blocked_rows: affine weights need sub-blocks and an act with derived sums");
    if (sub) {
        s.split(rx, rxs, rxc, sub);
        rx = rxc;
    }
    Func blk = s.rfactor(ry, u);
    // the act's codes (and on gemm tiles its scales) stay out of the inline:
    // staged in registers, below
    bool grouped = group && ts.mt > group;
    std::vector<Func> ac = decoders(rs, only({tag::codes}), 1), as = grouped ? decoders(rs, only({tag::scale}), 1) : std::vector<Func>{};
    std::vector<Func> dec = decoders(rs, all_but({}), 0, 1), rest = decoders(rs, grouped ? all_but({tag::codes, tag::scale}) : all_but({tag::codes}), 1);
    dec.insert(dec.end(), rest.begin(), rest.end());
    blk.update().eager_inline(dec);
    Func codes = blk.update().hoist_invariants()[0].change_type(Int(32)), part, inner;
    if (sub) {  // per sub-block (v): its scales times its codes' dot (inner)
        part = codes.update().rfactor(rxs, v);
        inner = part.update().hoist_invariants()[0];
    }
    Stage cu = (sub ? inner : codes).update();
    cu.split(rx, rp, rx, chunk).split(rx, rq, ri, 4);
    Func quads = cu.rfactor(rq, q);
    s.split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    Func acc = s.rfactor(ryi, bacc);
    LoopLevel at(acc, grouped ? VarOrRVar(mo) : VarOrRVar(ryo)), per_sub;
    s.reorder(n, ryi).vectorize(n).unroll(ryi).unroll(m);
    for (Func f : {blk, codes}) {
        f.compute_at(at).bound_storage(u, ts.interleave).vectorize_all_stages(n).unroll_all_stages(m, u);
    }
    if (!sub) {
        cu.reorder(rq, n).atomic().vectorize(rq);
        // gemm tiles: per act record, piece-major, each piece of its codes
        // staged just before use (the record's sums the only ones live)
        Stage qu = quads.update();
        grouped ? qu.split(m, mo, mi, group).reorder(ri, q, n, mi, rp, mo, u) : qu.reorder(ri, q, n, rp, u, m);
        qu.atomic().vectorize(ri);
        quads.reorder_storage(q, n, m).compute_at(at).bound_storage(u, ts.interleave).vectorize_all_stages(q, n).unroll_all_stages(m, mi, mo, rp, u);
    } else {
        // a loop over groups of sub-blocks, each unrolled (fully unrolled, the
        // codes' loads are scheduled early and spill); groups larger than pairs
        // where a bit field's position varies across pairs (constant shifts)
        RVar rxso("rxso"), rxsi("rxsi");
        auto bx = as_const_int(r.x.extent());
        int g = sharing_group(shared, 2, bx ? (int)*bx / sub : 2);
        codes.update().reorder(n, m, rxs, u).split(rxs, rxso, rxsi, g).unroll(rxsi);
        per_sub = LoopLevel(codes, rxsi);
        part.compute_at(per_sub).vectorize_all_stages(n).unroll_all_stages(v, u, m);
        inner.compute_at(per_sub).vectorize_all_stages(n).unroll_all_stages(v, u, m);
        cu.reorder(n, rq, v, u, m).unroll(rq);
        quads.reorder_storage(q, n, v, u, m).compute_at(per_sub).vectorize_all_stages(q, n).unroll_all_stages(v, u, m, rp);
        quads.update().reorder(ri, q, n, rp, v, u, m).atomic().vectorize(ri);
    }
    // a piece's act codes then are a lane of a register: by-element sdot
    for (Func a : ac) {
        if (calls(quads, a)) {
            Func ai = a.in(quads);
            Var k = ai.args()[0];
            if (grouped && ai.dimensions() == 3) {  // (code, block, row)
                Var b = ai.args()[1], am = ai.args()[2], ko("ko"), ki("ki"), kv("kv"), sko, ski;
                ai.split_storage(k, sko, ski, 4).reorder_storage(ski, am, sko, b);
                ai.split(k, ko, ki, 4).reorder(ki, am, ko).fuse(ki, am, kv).unroll(ko);
                k = kv;
            }
            ai.compute_at(sub ? per_sub : grouped ? LoopLevel(quads, rp) :
                                                    LoopLevel(quads, u))
                .vectorize(k);
        }
    }
    // the record's scales: one load, each row's a lane (fmla by element)
    for (Func a : as) {
        if (calls(blk, a)) {
            Func ai = a.in(blk);
            // rows innermost: the block's extent is a guarded min(), not 1
            ai.reorder_storage(ai.args()[1], ai.args()[0]).compute_at(at).vectorize(ai.args()[1]);
        }
    }
    Stage au = acc.update();
    grouped ? au.split(m, mo, mi, group).reorder(n, bacc, mi, mo, ryo) : au.reorder(n, bacc, m, ryo);
    acc.compute_at(tile_at(out, st)).vectorize_all_stages(n).unroll_all_stages(bacc, m, mi, mo);
    schedule_sums(d, out, st, acc, n, m, ts, r, true, 16, true);
}

// An i8mm stage's 2 x 2 sub-tiles (smmla's results), the loops `order`
// (innermost first). Guarded: this code is also in other tiles' branches,
// never run there, whose bounds must not grow past their tiles.
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

// The integer path on i8mm smmla: nt x mt tiles of 2 x 2 sub-tiles, each 4
// dense lanes of the intermediates' storage, so the accumulators stay in
// registers. `records`: the weights in records of ts.nt rows. `act_records`:
// the act too (q8_K.4x8). `per` > 1: weight blocks of `per` act blocks
// (scale-only weights), the weight's scale once per weight block.
inline void blocked_mmla(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r,
                         const std::vector<ApproximationResult> &rs, const Target &t, bool derived = false, int sub = 0, int act_sub = 0, bool records = false, bool act_records = false,
                         int per = 1, const Knobs &k = Knobs()) {
    bool fold_offsets = k.fold == Fold::All, gpr_scales = k.gpr_scales, index_words = k.index_words;
    std::vector<int> shared = sub ? shared_bits(dot, r, sub) : std::vector<int>{};  // (dot as defined)
    Stage s = tile_out(out, st, dot, cond, ts, r, records);
    Var n = dot.args()[0], m = dot.args()[1], nio("nio"), nii("nii"), mio("mio"), mii("mii"), u("u"), v("v"), bacc("bacc"), w("w");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rxk("rxk"), ryo("ryo"), ryi("ryi"), ryw("ryw"), ryp("ryp");
    bool aff = affine(rs);
    if (per > 1 && (aff || derived || sub || records)) throw std::invalid_argument("blocked_mmla: weight blocks of several blocks, scale-only weights");
    Sums d;
    // records: the sub-blocks' decoded scales (and mins) their own stage
    std::vector<Func> kept = records && sub ? scale_decoders(rs) : std::vector<Func>{};
    // gpr_scales: the plain weights' sub-block scales too, in GPRs (below)
    bool swar = gpr_scales && sub && !records;
    if (swar) kept = scale_decoders(rs);
    if (derived && derived_sums(out, st, s, r, rs, d, act_sub, records ? sub : 0, kept, fold_offsets)) aff = false;
    // With the act's records: r.x split first by the period over which the
    // bit fields' positions repeat (sharing_group: q6_K's 128 values), if
    // within it a bit no load shares lies below one some do, so the loads
    // stay dense (split in sub-blocks first, the % 128 stays: gathers).
    auto bx = as_const_int(r.x.extent());
    RVar rxp("rxp");
    Var vp("vp");
    int period = 0;
    if (sub && act_records && bx) {
        int g = sharing_group(shared, 2, (int)*bx / sub);
        bool zero = false, beneath = false;
        for (int b = 0; (1 << b) < g; b++) {
            beneath |= zero && shared[b];
            zero |= !shared[b];
        }
        if (g < *bx / sub && beneath) period = g * sub;
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
        // With the act's records, a loop over groups of sub-blocks (fully
        // unrolled spills: 4-7% slower), those reading the same bytes adjacent
        // (sharing_order); per period, bits sharing no load loop inside.
        Stage cu = codes.update();
        int subs = bx ? (int)*bx / sub : 2;
        if (period) subs = period / sub;
        int group = act_records && !period ? sharing_group(shared, 2, subs) : subs;
        RVar rxso("rxso"), rxsi("rxsi"), g = rxs, at_sub;
        if (group < subs) {
            cu.split(rxs, rxso, rxsi, group);
            g = rxsi;
        }
        std::vector<VarOrRVar> order{nii, mii, nio, mio};
        std::vector<int> bits = act_records ? sharing_order(shared, group) : std::vector<int>{};
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
                if (!period || shared[b]) order.push_back(bv[b]);
            }
            for (int b : bits) {
                if (period && !shared[b]) order.push_back(bv[b]), loops++;
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
        // the sub-block's scales, decoded once across the record's rows (the
        // rows innermost per period: dense, not gathered)
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
        rest.compute_at(acc, ryo).bound_storage(u, ts.interleave).vectorize_all_stages(n).unroll_all_stages(m, u);
        rest.update().reorder(rxk, rxc, n, m, u).atomic().vectorize(rxk).vectorize(rxc);
    }
    schedule_sums(d, out, st, acc, n, m, ts, r, true, 16, records, act_records);
    // a tile-local wrapper (k is shared with the other tiles), per block:
    // four scales per word, the rows unrolled (SIMD across rows: 3-4% slower)
    for (Func k0 : swar ? kept : std::vector<Func>{}) {
        Func k = k0.in_callers({part, inner, codes, blk, mid, rest, d.sums, d.blk, d.sum, d.part, d.pair});
        if (!k.defined()) continue;
        Var kw("kw"), kb("kb");
        k.compute_at(acc, ryo)
            .split(k.args()[0], kw, kb, 4)
            .unroll(kw)
            .unroll(k.args()[1])
            .vectorize(kb, 4, VectorizeIn::SWAR)
            .unroll(k.args()[2]);
    }
    tables(rs, {blk, mid, codes, rest, part, inner}, LoopLevel(acc, ryo), t, index_words);
}

// The Funcs of e's encoder: the intermediates of the result that encodes e,
// minus its decode Funcs.
inline std::vector<Func> encoder_funcs(Func e, const std::vector<ApproximationResult> &rs) {
    std::vector<Func> dec;
    for (const ApproximationResult &res : rs) {
        std::vector<Func> d = res.decode_funcs();
        dec.insert(dec.end(), d.begin(), d.end());
    }
    for (const ApproximationResult &res : rs) {
        if (res.encoded[0].name() == e.name()) return without(res.intermediates, dec);
    }
    return {};
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

// An encoder computed per record, vectorized: its per-value Funcs across
// their elements, its reassociable reductions across the block (a
// strict_float sum, q1_0's, stays in order). `vec`: e itself vectorized
// (acts of values, not blocks).
inline void encoder(Func e, const std::vector<ApproximationResult> &rs, int vec = 0) {
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
    if (vec) e.vectorize(e.args()[0], vec, TailStrategy::GuardWithIf);
}

// The N = M = 1 schedule (GGML's vec_dot). Staged activations are encoded
// once, up front.
inline void vec_dot(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t, const Approximation &acc = Approximation(), const Knobs &k = Knobs()) {
    luts(rs, true);
    for (Func e : staged) {
        encoder(e.compute_root(), rs, fmt.act_block == 1 ? 4 * t.natural_vector_size<float>() : 0);
    }
    out.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);  // N = M = 1
    if (fmt.block == 1) {
        // 8 interleaved steps: f16 vec_dot 1.2x, gemv 1.05-1.1x of 4 (M3)
        acc.defined() ? flat_tree(out, 0, dots[0], Expr(), {1, 1, 4}, r, acc, staged) : flat(out, 0, dots[0], Expr(), {1, 1, 8}, r, t);
        return;
    }
    blocked(out, 0, dots[0], Expr(), {1, 1, k.interleave}, r, fmt.act_block == fmt.block, rs, t, true, fmt.sub, fmt.act_sub, k, true);
}

// The Metal tiles of out: threadgroups of `sg` simdgroups, each `rows(cols)`
// rows of `cols` columns, the widest cols <= 8 that M allows (gemv: nr rows
// of one column); N < nr * sg, a row per threadgroup. Edge tiles shift in.
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

// Metal: lut f per threadgroup, at out's loop `at`, in threadgroup memory.
inline void metal_lut(Func f, Func out, const Var &at) {
    Var p = f.args()[0], i = f.args()[1];
    f.compute_at(out, at).store_in(MemoryType::GPUShared).reorder(i, p).gpu_threads(i);
}

// Metal: the codebooks' luts, each per threadgroup in threadgroup memory, as
// GGML's kernel_mul_mv_iq4_nl_f32 (iq4_nl/iq4_xs/mxfp4 gemv 1.45-1.75x vs a
// select tree; M3 Max).
inline void metal_luts(Func out, const std::vector<ApproximationResult> &rs) {
    for (Func f : decoders(rs)) {
        if (register_lut(rs, f)) metal_lut(f, out, Var("no"));
    }
}

// g's private copies (clone_in) of the Funcs on its calls' paths to those
// `pick` selects; returns the copies of the picked.
inline std::vector<Func> clone_paths(Func g, const std::function<bool(const Func &)> &pick) {
    using namespace Internal;
    std::map<std::string, bool> memo;
    std::function<bool(const Function &)> reaches = [&](const Function &f) {
        auto it = memo.find(f.name());
        if (it != memo.end()) return it->second;
        memo[f.name()] = false;
        bool r = pick(Func(f));
        for (const auto &c : find_direct_calls(f)) {
            r = reaches(c.second) || r;
        }
        return memo[f.name()] = r;
    };
    std::vector<Func> picked;
    std::function<void(Func)> walk = [&](Func f) {
        for (const auto &c : find_direct_calls(f.function())) {
            if (!reaches(c.second)) continue;
            Func copy = Func(c.second).clone_in(f);
            if (pick(Func(c.second))) {
                picked.push_back(copy);
            } else {
                walk(copy);
            }
        }
    };
    walk(g);
    return picked;
}

// Metal, as GGML's kernel_mul_mv_*_f32: threadgroups of `sg` simdgroups, each
// rows of one column. A lane takes 2^vals values of a block: runs of 8 bytes
// times the values sharing their bytes (shared_bits()), the rest the block's
// lanes; `nr` rows for 16 values, half for 32 (256-value blocks: 4 rows
// 0.65-0.9x of 2, M3 Max). Two-level weights: per sub-block, the codes' dot
// times its scale. The lanes reduce across the simdgroup (simd shuffles).
// M > 1: rows x cols per simdgroup, a lane per value of every 32 (coalesced;
// the gemv lanes there 0.5-1.4x), behind dot's specialization.
inline void metal_rows(Func out, Func dot, const RDom &r, const std::vector<ApproximationResult> &rs, int sub = 0) {
    Var n = out.args()[0], m = out.args()[1], ns("ns"), ub("ub"), u("u"), l("l");
    RVar rx = r.x, ry = r.y, ryo("ryo"), ryi("ryi"), rl("rl");
    Expr M = out.output_buffer().dim(1).extent();
    std::optional<int64_t> bx = Internal::as_const_int(r.x.extent());
    int bits = 0;
    while (bx && (1 << bits) < *bx)
        bits++;
    if (!bx || *bx != 1 << bits || bits < 5) throw std::invalid_argument("metal_rows: blocks of 2^k >= 32 values");
    const int run = 3, vals = std::min(5, bits - 1);
    const int nr = std::max(1, 4 >> (vals - 4)), sg = 2;
    // the bits each kind: run, shared, the lane's
    enum Kind { Run,
                Shared,
                Lane };
    std::vector<Kind> kind(bits, Lane);
    for (int b = 0; b < run; b++) {
        kind[b] = Run;
    }
    std::vector<int> count = shared_bits(dot, r, 1, run), order;
    for (int b = run; b < bits; b++) {
        if (count[b]) order.push_back(b);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return count[a] > count[b]; });
    for (int b = bits; b-- > run;) {
        if (!count[b]) order.push_back(b);
    }
    for (int i = 0; i < vals - run; i++) {
        kind[order[i]] = Shared;
    }
    metal_tiles(out, dot, nr, sg, [](int cols) { return cols > 4 ? 2 : 4; });
    Var lx("lx");
    RVar ryk("ryk"), ryu("ryu"), rxo("rxo"), rxl("rxl");
    Stage sm = dot.update().specialize(M >= 2);
    if (bits > 5) sm.split(rx, rxo, rxl, 32);
    Func lanes = sm.rfactor(bits > 5 ? rxl : rx, lx);
    sm.atomic().gpu_lanes(bits > 5 ? rxl : rx);
    lanes.compute_at(out, ns).gpu_lanes(lx).unroll(n).unroll(m);
    Stage ls = lanes.update().split(ry, ryk, ryu, std::max(1, 4 >> (bits - 5)), TailStrategy::GuardWithIf);
    if (bits > 5) {
        ls.reorder(n, m, rxo, ryu, ryk, lx).unroll(rxo);
    } else {
        ls.reorder(n, m, ryu, ryk, lx);
    }
    ls.gpu_lanes(lx).unroll(n).unroll(m).unroll(ryu);
    Stage s = dot.update();
    // r.x in segments of one kind, from the lowest
    struct Seg {
        RVar r;
        Kind kind;
    };
    std::vector<Seg> segs;
    RVar rest_x = rx;
    for (int b = 0; b < bits;) {
        int e = b + 1;
        while (e < bits && kind[e] == kind[b])
            e++;
        std::string name = std::string(kind[b] == Run ? "rxi" : kind[b] == Shared ? "rxn" :
                                                                                    "rxh") +
                           std::to_string(segs.size());
        if (e < bits) {
            RVar in(name), o("rx" + std::to_string(segs.size()) + "o");
            s.split(rest_x, o, in, 1 << (e - b));
            segs.push_back({in, kind[b]});
            rest_x = o;
        } else {
            segs.push_back({rest_x, kind[b]});
        }
        b = e;
    }
    std::vector<std::pair<RVar, Var>> lane_vars, acc_vars, sub_vars;
    std::vector<RVar> lane_rs, inner_rs, sub_rs;
    // (runs first: innermost)
    std::stable_partition(segs.begin(), segs.end(), [](const Seg &g) { return g.kind == Run; });
    for (const Seg &g : segs) {
        if (g.kind == Lane) {
            lane_vars.emplace_back(g.r, Var("uh" + std::to_string(lane_vars.size())));
            acc_vars.emplace_back(g.r, Var("ua" + std::to_string(acc_vars.size())));
            lane_rs.push_back(g.r);
        } else if (sub && g.kind == Shared) {
            sub_vars.emplace_back(g.r, Var("v" + std::to_string(sub_vars.size())));
            sub_rs.push_back(g.r);
        } else {
            inner_rs.push_back(g.r);
        }
    }
    lane_vars.emplace_back(ry, u);
    Func blk = s.rfactor(lane_vars), rest, mid;
    Func codes = block_dot(blk, rs, false, rest, mid, affine(rs));
    // per sub-block (v): its scale times its codes' dot (inner)
    std::vector<std::tuple<Func, Func, Func>> subs;
    for (Func f : {codes, rest}) {
        if (sub_vars.empty() || !f.defined()) continue;
        Func part = f.update().rfactor(sub_vars);
        subs.emplace_back(f, part, part.update().hoist_invariants()[0]);
    }
    s.split(ry, ryo, ryi, 1 << (5 + vals - bits), TailStrategy::GuardWithIf);
    acc_vars.emplace_back(ryi, ub);
    Func acc = s.rfactor(acc_vars);
    // the lane: its segments, then its block (lowest first)
    RVar fr = lane_rs[0];
    Var fv = acc_vars[0].second;
    std::vector<Var> storage;
    for (size_t i = 0; i < acc_vars.size(); i++) {
        storage.push_back(acc_vars[i].second);
    }
    storage.insert(storage.end(), {n, m});
    acc.reorder_storage(storage).compute_at(out, ns);
    std::vector<RVar> lrs = lane_rs;
    lrs.push_back(ryi);
    Stage au = acc.update();
    for (size_t i = 1; i < lrs.size(); i++) {
        RVar f(i + 1 < lrs.size() ? "rl" + std::to_string(i) : "rl");
        s.fuse(fr, lrs[i], f);
        fr = f;
        Var g(i + 1 < lrs.size() ? "l" + std::to_string(i) : "l");
        acc.fuse(fv, acc_vars[i].second, g);
        au.fuse(fv, acc_vars[i].second, g);
        fv = g;
    }
    s.atomic().gpu_lanes(rl);
    acc.reorder(n, m, l).gpu_lanes(l).unroll(n);
    au.reorder(n, m, ryo, l).gpu_lanes(l).unroll(n);
    // bound_storage: also in the K tail: registers
    for (Func f : {blk, mid, codes, rest}) {
        if (f.defined()) f.compute_at(acc, ryo).bound_storage(u, 1).unroll(f.args()[0]);
    }
    blk.update().unroll(blk.args()[0]);
    for (Func f : {codes, rest}) {
        if (!f.defined()) continue;
        Stage fs = f.update();
        if (!sub_vars.empty()) {
            // per sub-block, its dot (inner), then times its scale (part)
            std::vector<VarOrRVar> o{f.args()[0]};
            o.insert(o.end(), sub_rs.begin(), sub_rs.end());
            fs.reorder(o).unroll(f.args()[0]);
            for (const RVar &v : sub_rs) {
                fs.unroll(v);
            }
            continue;
        }
        std::vector<VarOrRVar> o{f.args()[0]};
        o.insert(o.end(), inner_rs.begin(), inner_rs.end());
        fs.reorder(o).unroll(f.args()[0]);
        for (const RVar &v : inner_rs) {
            fs.unroll(v);
        }
    }
    for (auto [f, part, inner] : subs) {
        for (Func g : {part, inner}) {
            g.compute_at(f, sub_rs[0]).bound_storage(u, 1);
            for (int i = 0; i <= g.num_update_definitions(); i++) {
                Stage gs = i ? g.update(i - 1) : Stage(g);
                for (const Var &v : g.args()) {
                    gs.unroll(v);
                }
            }
        }
        std::vector<VarOrRVar> o{inner.args()[0]};
        o.insert(o.end(), inner_rs.begin(), inner_rs.end());
        inner.update().reorder(o);
        for (const RVar &v : inner_rs) {
            inner.update().unroll(v);
        }
    }
}

// Metal on float weights, as GGML's kernel_mul_mv_f16_f32_4: metal_tiles of
// 2 rows (gemv) or 4 rows of up to 8 columns. A lane takes `c` contiguous
// values of every 32 * c (coalesced; merged into half4/float4 loads), each
// output a scalar accumulator (float4 ones spill for M > 1). Staged acts get
// a kernel of their own (inlined, every tile would encode its columns).
inline void metal_flat(Func out, Func dot, const RDom &r, const std::vector<Func> &staged) {
    const int nr = 2, sg = 2, c = 16;
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

// The f16 operands of a Metal gemm: the weight's compute format's encoded
// values (wh: its decoded values rounded to f16, e.g. q4_0:f16) or f16
// storage's, and the act's (xh: an f16 compute format's, staged, or f16
// storage's), else false.
inline bool f16_operands(const std::vector<ApproximationResult> &rs, const std::vector<Func> &staged, const Formats &fmt, Func &wh, Func &xh) {
    const ApproximationResult *w = fmt.wcompute ? &rs.back() : fmt.block == 1 && !rs.empty() ? &rs[0] :
                                                                                               nullptr;
    size_t acts = rs.size() - fmt.wcompute;
    if (!w || w->encoded.size() != 1) return false;
    wh = w->encoded[0];
    xh = staged.size() == 1 ? staged[0] : acts == 2 && rs[1].encoded.size() == 1 ? rs[1].encoded[0] :
                                                                                   Func();
    return wh.type() == Float(16) && xh.defined() && xh.type() == Float(16);
}

// The f32 operands of a Metal gemm on exact weights (wh: the weights' decoded
// values; xh: the acts dot multiplies them by), else false. Not two-level
// formats: the gemm specialization's negation in the rows' SkipStages flags
// makes M = 1 1.17-1.28x slower (q4_K, q2_K, iq4_xs; M3 Max).
inline bool f32_operands(Func dot, const std::vector<ApproximationResult> &rs, const std::vector<Func> &staged, const Formats &fmt, Func &wh, Func &xh) {
    using namespace Internal;
    if (fmt.wcompute || !staged.empty() || rs.empty() || fmt.block % 8 || fmt.sub) return false;
    wh = rs[0].replacement;
    const Add *sum = dot.update_value().as<Add>();
    const Mul *p = sum ? sum->b.as<Mul>() : nullptr;
    const Call *u = p ? p->a.as<Call>() : nullptr, *v = p ? p->b.as<Call>() : nullptr;
    if (!u || !v || u->call_type != Call::Halide || v->call_type != Call::Halide) return false;
    if (v->name == wh.name()) std::swap(u, v);
    if (u->name != wh.name()) return false;
    xh = Func(Function(v->func));
    return dot.type() == Float(32) && wh.type() == Float(32) && xh.type() == Float(32);
}

// Metal gemm on f16 or f32 operands (wh, xh), as GGML's kernel_mul_mm, on the
// simdgroup_matrix unit, for `o` (out's specialization). A threadgroup takes
// a bn x bm tile, its simdgroups wn x 1 sub-tiles of 8 * tn x 8 * tm, each
// 8 x 8 an accumulator in Tile memory. Per step of K the operands are staged
// in threadgroup memory, then loaded as 8 x 8 fragments. Tiles by M: 64 x 64
// on blocked f16 operands (or 64 x 56), else 64 x 32 (M >= 256, N >= 64),
// 32 x 32, 16 x 16, 16 x 8; steps of 64 values. On f16 operands at
// 16 < M < 32 one 32 x 32 tile, guarded (decoding the weights once, not in
// two shifted tiles): out reads it from a threadgroup copy (edge; a guarded
// store from Tile memory doesn't lower), its stores predicated (guarded calls
// would give every tile SkipStages flags); the rows past M it computes read
// the last act row (the algorithm clamps them).
inline void metal_mm(Stage o, Func out, Func dot, Func edge, Func wh, Func xh, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs) {
    if (fmt.block % 8 && fmt.block != 1) throw std::invalid_argument("metal_mm: flat, or blocks of a multiple of 8 values");
    const int t = 8;
    // (names apart from the other specializations' loops, which compute
    // other Funcs at theirs)
    Var n = out.args()[0], m = out.args()[1], ni("mm_ni"), mi("mm_mi"), nw("mm_nw"), mw("mm_mw"), nt("mm_nt"), mt("mm_mt");
    RVar rx = r.x, ry = r.y, ks("ks"), kb("kb"), ko("ko"), kt("kt"), rxk("rxk");
    Expr M = out.output_buffer().dim(1).extent(), N = out.output_buffer().dim(0).extent(), nb = r.y.extent();
    struct Shape {
        Expr cond;
        int tn, tm, wn;
        bool guard = false;
    };
    const bool h = wh.type() == Float(16), hb = h && fmt.block > 1;
    // (64 x 56 where it computes fewer rows past M than 64 x 64)
    Expr m56 = (M + 55) / 56 * 56 < (M + 63) / 64 * 64;
    std::vector<Shape> shapes{{M > 16 && M < 32 && h, 1, 4, 4, true}, {hb ? M >= 256 && N >= 64 && m56 : Internal::const_false(), 2, 7, 4}, {M >= 256 && N >= 64, 2, hb ? 8 : 4, 4}, {M >= 32, 1, 4, 4}, {M >= 16, 1, 2, 2}, {Expr(), 1, 1, 2}};
    // dot reads wh and xh directly, through threadgroup copies (ws, xs) and
    // their fragments (wf, xf)
    dot.update().eager_inline_all({wh, xh});
    Func ws = wh.in(dot), xs = xh.in(dot), wf = ws.in(dot), xf = xs.in(dot);
    // the weights' luts the gemm's own, per threadgroup (iq4_nl/mxfp4 M = 16
    // 1.03-1.06x, M = 64..128 0.94-0.98x vs packed muxes; M3 Max)
    std::set<std::string> staged;
    for (Func f : decoders(rs)) {
        if (register_lut(rs, f)) staged.insert(f.name());
    }
    for (Func f : clone_paths(ws, [&](const Func &f) { return staged.count(f.name()) > 0; })) {
        metal_lut(f, out, n);
    }
    Stage sd = Stage(dot), su = dot.update();
    std::function<void()> edged;
    for (const Shape &sh : shapes) {
        if (sh.cond.defined() && Internal::is_const_zero(sh.cond)) continue;
        const int bn = t * sh.tn * sh.wn, bm = t * sh.tm;
        auto tile = [&](Stage st) {
            if (sh.guard) st.tile({n, m}, {nw, mw}, {ni, mi}, {t * sh.tn, t * sh.tm}, {TailStrategy::Auto, TailStrategy::RoundUp});
            else
                st.tile(n, m, nw, mw, ni, mi, t * sh.tn, t * sh.tm);
            return st.tile(ni, mi, nt, mt, t, t).gpu_threads(nw, mw).unroll(ni).unroll(mi).vectorize(nt).vectorize(mt);
        };
        Stage so = o.specialize(sh.cond.defined() ? sh.cond : Internal::const_true());
        if (sh.guard) {
            Var sw("mm_sw");
            so.split(n, n, ni, bn, TailStrategy::ShiftInwards).split(m, m, mi, bm / sh.wn, TailStrategy::PredicateStores).split(m, m, sw, sh.wn, TailStrategy::RoundUp).reorder(ni, mi, sw, n, m);
            so.gpu_blocks(n, m).gpu_lanes(ni).gpu_threads(sw);
            edged = [=]() mutable {
                edge.compute_at(out, n).store_in(MemoryType::GPUShared);
                tile(edge);
            };
        } else {
            so.eager_inline(edge);
            so.tile(n, m, ni, mi, bn, bm, TailStrategy::ShiftInwards).tile(ni, mi, nw, mw, ni, mi, t * sh.tn, t * sh.tm).tile(ni, mi, nt, mt, t, t);
            so.gpu_blocks(n, m).gpu_threads(nw, mw).unroll(ni).unroll(mi).vectorize(nt).vectorize(mt);
        }
        tile(sd.specialize(sh.cond));
        Stage u = su.specialize(sh.cond);
        // ks: the steps of K; kb: their blocks (or, for blocks longer than a
        // step or flat, ko: the step's 8-value pieces)
        auto steps = [&](Stage st, int kstep) {
            tile(st);
            if (fmt.block == 1) {
                st.split(ry, ks, rxk, kstep).split(rxk, ko, kt, t).reorder(kt, nt, mt, ni, mi, ko, rx, nw, mw, ks);
            } else if (fmt.block >= kstep) {
                st.split(rx, ks, rxk, kstep).split(rxk, ko, kt, t).reorder(kt, nt, mt, ni, mi, ko, nw, mw, ks, ry);
            } else {
                st.split(ry, ks, kb, kstep / fmt.block).split(rx, ko, kt, t).reorder(kt, nt, mt, ni, mi, ko, kb, nw, mw, ks).unroll(kb);
            }
            st.unroll(ko).atomic().vectorize(kt);
        };
        if (fmt.block == 1) {
            steps(u.specialize(nb % 64 == 0), 64);
            steps(u, 32);  // (metal_gemm: K % 32 == 0)
        } else {
            if (fmt.block < 64) steps(u.specialize(nb % (64 / fmt.block) == 0), 64);
            steps(u, std::min(fmt.block, 64));
        }
        // each lane 4 values of K of a row of the step's weights or acts
        // (blocked (j, b, i) or flat (k, i)), unrolled (f32 acts on 64 x 56
        // 0.92 -> 1.0x of GGML's)
        for (Func f : {ws, xs}) {
            std::vector<Var> a = f.args();
            Var l("l"), ln("ln"), lt("lt"), sw("sw"), jv("jv");
            Stage s = f.specialize(sh.cond);
            s.split(a[0], l, jv, 4);
            for (size_t i = 1; i < a.size(); i++) {
                Var fl("l" + std::to_string(i));
                s.fuse(l, a[i], fl);
                l = fl;
            }
            s.split(l, l, ln, 32).split(l, lt, sw, sh.wn).reorder(jv, lt, ln, sw);
            s.gpu_lanes(ln).gpu_threads(sw).vectorize(jv).unroll(lt);
        }
    }
    if (edged) edged();
    dot.compute_at(out, n).store_in(MemoryType::Tile);
    for (Func f : {ws, xs}) {
        f.compute_at(dot, ks).store_in(MemoryType::GPUShared);
    }
    // the operand fragments of each 8 values of K: xf's 8 x 8 * tm, wf's 8 * tn x 8
    // (its storage [row, k]: a fragment's rows are the weight's)
    for (Func f : {xf, wf}) {
        std::vector<Var> a = f.args();
        Var j = a[0], i = a.back(), jo("jo"), jt("jt"), io("io"), it("it");
        if (f.name() == wf.name()) {
            if (a.size() == 3) f.reorder_storage(i, j, a[1]);
            else
                f.reorder_storage(i, j);
        }
        f.compute_at(dot, ko).store_in(MemoryType::Tile).split(j, jo, jt, t).split(i, io, it, t);
        if (a.size() == 3) {
            f.reorder(jt, it, jo, io, a[1]).unroll(a[1]);
        } else {
            f.reorder(jt, it, jo, io);
        }
        f.unroll(jo).unroll(io).vectorize(jt).vectorize(it);
    }
}

// Metal on f16 or f32 operands: metal_mm where M >= min_m and N >= 32 (flat:
// K % 32 == 0 and M * N >= 2^16; below it flat's rows win, 1.2-2.6x at M =
// 8..32, N = 512..2048), else metal_rows (flat: metal_flat) on a clone of
// dot, one kernel but flat's staged acts. min_m: 8 on f16 (GGML's), 16 on f32 (below
// it the f32 tiles lose on mxfp4, iq4_nl, q4_1).
inline void metal_gemm(Func out, Func dot, Func wh, Func xh, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs, int min_m,
                       const std::vector<Func> &staged = {}) {
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    // out reads dot through a wrapper (via): the gemm's branch inlines it at
    // once, reading edge (dot's copy, inlined unless guarded); then via reads
    // pass, reading the clone, and the other branches inline both
    Func via = dot.in(out), edge = dot.in(via);
    Expr cond = M >= min_m && N >= 32;
    if (fmt.block == 1) cond = cond && r.y.extent() % 32 == 0 && cast<int64_t>(M) * N >= (1 << 16);
    Stage mm = Stage(out).specialize(cond).eager_inline(via);
    Func pass = edge.clone_in(via);
    Func rows = dot.clone_in(pass);
    out.eager_inline(via, pass);
    metal_mm(mm, out, dot, edge, wh, xh, r, fmt, rs);
    if (fmt.block > 1) {
        metal_rows(out, rows, r, rs, fmt.sub);
        return;
    }
    // the rows' own staged acts (their kernel skipped under the gemm, whose
    // tiles stage theirs)
    std::set<std::string> names;
    for (const Func &e : staged) {
        names.insert(e.name());
    }
    metal_flat(out, rows, r, clone_paths(rows, [&](const Func &f) { return names.count(f.name()) > 0; }));
}

// Rowwise formats (schemes/kleidiai.h: qsi8cx weights, qai8dx acts): one
// scale per row, so the codes' dot accumulates in int32 over all of K. dot's
// increment splits on the act's codes: the codes' dot (returned) and the
// weight's code sum, read from its derived port (read_port).
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

// The rowwise gemv: per tile of ts.nt rows, the codes' dot in int32 sdot
// lanes over all of K. Records of rows in `chunk`-code pieces: lanes across
// the rows (KleidiAI's qsi8cxp4x8 gemv); planar (chunk 0): 16-code lanes.
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
        quads.reorder_storage(q, n, m).compute_at(tile).vectorize_all_stages(q, n).unroll_all_stages(m, rp);
    } else {
        cu.split(rx, ro, ri, 4);
        Func lanes = cu.rfactor(ro, q);
        cu.reorder(ro, n).atomic().vectorize(ro);
        lanes.update().reorder(ri, q, n, ry, m).atomic().vectorize(ri);
        lanes.reorder_storage(q, n, m).compute_at(tile).vectorize_all_stages(q).unroll_all_stages(n, m);
    }
    codes.compute_at(tile).unroll_all_stages(n, m);
}

// The rowwise gemm on i8mm smmla, as blocked_mmla with no per-block work.
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

// A rowwise act encoded in the pipeline (qai8dx): row ranges, scale and zero
// point once per row, then the codes, parallel over rows. With `pairs`, the
// codes stored in groups of 8 rows (M rounded up), 8-code pieces adjacent,
// as KleidiAI's qai8dxp packs them for smmla; one row (gemv) stays dense.
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

// mul_mat on rowwise formats: gemm on smmla tiles where the target has i8mm,
// 8 x 8 (16 accumulators, as KleidiAI's 16 x 4), else 4 x 8; gemv tiles of 4
// rows, in tasks of 16 rows.
inline void rowwise_mul_mat(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt,
                            const std::vector<ApproximationResult> &rs, const std::vector<Func> &staged, const Target &t) {
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    if (dots.size() > 1 || fmt.act_rows > 1) throw std::invalid_argument("rowwise: act records of one row");
    int chunk = fmt.rows > 1 ? fmt.chunk : 0;
    for (Func e : staged) {
        rowwise_encoder(e, out, rs, M, t.has_feature(Target::ARMI8MM));
    }
    Func dot = dots[0];
    Tiles mmla8x8{8, 8, 1, 32, 32, true}, mmla4x8{4, 8, 1, 32, 32, true}, mmla4x2{4, 2, 1, 32, 32, true}, rows{4, 1, 1, 16}, single{1, 1, 1, 16};
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

// mul_mat: gemm tiles of the outputs (smmla with i8mm, else sdot; whole act
// records on 4 x 12/16/20 tiles), in tasks of up to 32 x 32 outputs,
// balanced over the threads; gemv (M = 1) is vec_dot's recipe on row pairs
// (single rows for N = 1, staged tables, or without `row_pairs`). Edge tiles
// shift inwards. With act records of fmt.act_rows > 1 rows, out's first
// update is the rows in whole records (dots[1]), its second M = 1's
// (dots[0]).
inline void mul_mat(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t, const Approximation &acc = Approximation(), const Knobs &k = Knobs()) {
    luts(rs, !t.has_gpu_feature());
    if (t.has_feature(Target::Metal)) {
        if (acc.defined()) throw std::invalid_argument("metal: dots accumulated in f32");
        if (fmt.act_rows > 1 || fmt.rows > 1) throw std::invalid_argument("metal: weights and acts one row per record");
        Func wh, xh;
        if (f16_operands(rs, staged, fmt, wh, xh)) {
            metal_gemm(out, dots[0], wh, xh, r, fmt, rs, 8, staged);
            metal_luts(out, rs);
            return;
        }
        if (fmt.block == 1) {
            metal_flat(out, dots[0], r, staged);
            return;
        }
        if (f32_operands(dots[0], rs, staged, fmt, wh, xh)) {
            metal_gemm(out, dots[0], wh, xh, r, fmt, rs, 16);
            metal_luts(out, rs);
            return;
        }
        if (!staged.empty() || fmt.wcompute) throw std::invalid_argument("metal: blocked weights on acts stored as computed, or a weight compute format without f16 acts");
        metal_luts(out, rs);
        metal_rows(out, dots[0], r, rs, fmt.sub);
        return;
    }
    if (fmt.rowwise) {
        rowwise_mul_mat(out, dots, r, fmt, rs, staged, t);
        return;
    }
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    bool integer = fmt.act_block == fmt.block, whole = fmt.act_rows > 1;
    Expr Mr = select(M > 1, M, 0);  // (as matmul's: whole records' rows)
    const int fv = 4 * t.natural_vector_size<float>();
    for (size_t i = 0; i < staged.size(); i++) {
        // staged[i] encodes dots[i]'s act, in tasks of up to 16 chunks of
        // blocks; with whole records, staged[0] (M = 1's row) one per task
        Func e = staged[i];
        Var b = e.args()[0], rec = e.args().back(), bo("bo"), t("t");
        Stage s = e.compute_root().specialize(M > 1);
        if (whole && !i) {
            s.parallel(rec);
        } else {
            Expr B = r.y.extent(), recs = whole ? (Mr + fmt.act_rows - 1) / fmt.act_rows : M;
            std::vector<int> cs;
            for (int c = 1; c <= 16; c++) {
                cs.push_back(c);
            }
            Expr chunks = balanced(cs, 4096, [&](int c) { return std::pair{recs * c, (B + c - 1) / c * fmt.act_block * fmt.act_rows}; });
            s.split(b, bo, b, (B + chunks - 1) / chunks, TailStrategy::GuardWithIf).fuse(rec, bo, t).parallel(t);
        }
        encoder(e, rs, fmt.act_block == 1 ? fv : 0);
        if (fmt.act_block == 1) s.vectorize(b, fv, TailStrategy::GuardWithIf);
    }
    if (fmt.block == 1) {
        // Float weights: 4 x 2 tiles, else single rows (row pairs: gemv 0.65x
        // GGML's at K = 14336; N < 4's gemm 1.2-2.4x slower).
        if (whole || fmt.rows > 1) throw std::invalid_argument("weights without blocks: one row per record, acts too");
        Tiles fg{4, 2, 1, 32, 32, true}, fr{1, 1, 8, 16};
        // (GGML's tree: four f16 vectors per output; 4 x 2 tiles spill)
        if (acc.defined()) fg = {2, 2, 1, 32, 32, true};
        for (auto [c, ts] : Chain{{N >= fg.nt && M >= fg.mt, fg}, {Expr(), fr}}) {
            acc.defined() ? flat_tree(out, 0, dots[0], c, ts, r, acc, staged) : flat(out, 0, dots[0], c, ts, r, t);
        }
        return;
    }
    Tiles mmla8{4, 8, 1, 32, 32, true}, gemm{4, 4, 1, 32, 32, true}, pairs{2, 1, 2, 16}, rows{1, 1, 2, 16};
    bool mmla_ok = integer && t.has_feature(Target::ARMI8MM);
    // Weight records in 4-code pieces: by-element sdot on the same tiles (on M3,
    // KleidiAI's dotprod gemm beats both i8mm ones)
    bool lanes = whole && integer && fmt.rows > 1 && fmt.chunk == 4, tiled = mmla_ok || lanes;
    // Two-level records of several rows (q4_K_8x8): the rest on their gemv;
    // with i8mm, on whole act records too (q8_K.4x8), one record per tile
    bool recs = integer && fmt.rows > 1 && fmt.sub, rmmla = mmla_ok && recs;
    Tiles rm{fmt.rows, 4, 1, 32, whole ? 128 : 32, true};
    // Whole records: the tiles' rows past Mr read clamped records, so one
    // row tile (dot's bound) covers every record, with mmla when there is.
    Func dr = dots.back();
    auto fits = [&](const Tiles &ts, Expr m) { return whole ? N % ts.nt == 0 && N >= ts.nt : N >= ts.nt && m >= ts.mt; };
    // Whole records' row tiles: 12, 16 or 20 rows, whichever pads Mr least
    // (ties: 16, then 20); one record 16 x 4, two 8 rows. A chain, so the
    // later branches learn each condition false (dr's bound).
    Tiles t16{4, 16, 1, 32, 128, true}, t20{4, 20, 1, 32, 120, true}, t12{4, 12, 1, 32, 120, true}, t8{4, 8, 1, 32, 128, true}, wide{16, 4, 1, 32, 128, true};
    auto padded = [&](const Tiles &ts) { return (Mr + ts.mt - 1) / ts.mt * ts.mt; };
    Expr fewest = min(padded(t12), padded(t16), padded(t20)), odd_n = !fits(gemm, Mr);
    Chain tiles{{Mr <= wide.mt && fits(wide, Mr), wide}, {Mr <= t8.mt, t8}, {padded(t16) == fewest, t16}, {padded(t20) == fewest, t20}, {Expr(), t12}};
    if (whole && rmmla) {  // N a multiple of the records' rows
        dr.bound_extent(dr.args()[1], rm.mt);
    } else if (whole) {  // odd N first, as tall as t16; the bound is then constant in each branch
        blocked(out, 1, dr, odd_n, {1, tiled ? t16.mt : gemm.mt, 1, 32, 32, true}, r, integer, rs, t);
        Expr mt = t12.mt;
        for (size_t i = tiles.size() - 1; i-- > 0;) {
            mt = select(tiles[i].first, tiles[i].second.mt, mt);
        }
        dr.bound_extent(dr.args()[1], tiled ? select(odd_n, t16.mt, mt) : Expr(gemm.mt));
    }
    if (whole && tiled && !rmmla) {
        for (auto [c, ts] : tiles) {
            lanes ? blocked_rows(out, whole, dr, c, ts, r, fmt.chunk, rs, fmt.act_rows) : blocked_mmla(out, whole, dr, c, ts, r, rs, t, false, 0, 0, false, false, fmt.sub ? 1 : fmt.per);
        }
    } else if (rmmla) {
        // whole records of rows (from 0 mod their rows: their addresses dense)
        // (dr's act is the last result: its derived ports, rs[1]'s for derived_sums)
        std::vector<ApproximationResult> rr = rs;
        if (whole) std::swap(rr[1], rr.back());
        blocked_mmla(out, whole, dr, whole ? Expr() : fits(rm, M), rm, r, rr, t, true, fmt.sub, fmt.act_sub, true, whole, 1, k);
    } else if (mmla_ok) {
        blocked_mmla(out, whole, dr, fits(mmla8, M), mmla8, r, rs, t, true, fmt.sub, fmt.act_sub, false, false, 1, k);
    }
    // (records without i8mm: their gemv per column; blocked()'s tiles gather)
    if (!(recs && !whole) && (!whole || !tiled)) blocked(out, whole, dr, whole ? Expr() : fits(gemm, M), gemm, r, integer, rs, t, !whole, fmt.sub, fmt.act_sub, k);
    // The rows past Mr (with whole records, M = 1's): no tasks when there are none.
    Expr rest = whole ? M == 1 : Expr();
    if (integer && fmt.rows > 1) {  // N is a multiple of the records' rows
        Tiles rt{fmt.rows, 1, 2, 16, 0, false, rest};
        // (a two-level record, 8 rows of q4_K: 1152 bytes, one per iteration)
        if (fmt.sub) {
            rt.interleave = 1;
            blocked_rows(out, 2 * whole, dots[0], Expr(), rt, r, fmt.chunk, rs, 0, fmt.sub, fmt.act_sub);
            return;
        }
        // Codes decoded from a field: two records per tile where N allows
        // (gemv 1.02-1.1x; q8_0's, not decoded, 1.02-1.11x slower on ten
        // threads at K = 14336), unless records over 11 KB on <= 4 threads.
        std::vector<Func> cc = rs[0].decoded_components(tag::codes), st = rs[0].stored_components(tag::codes);
        if (cc.size() == 1 && std::any_of(st.begin(), st.end(), [&](const Func &f) { return f.type() != cc[0].type(); })) {
            Tiles r2 = rt;
            r2.nt = 2 * fmt.rows;
            Expr two = N % r2.nt == 0;
            if (fmt.row_bytes.defined()) two = two && (fmt.row_bytes <= 11 * 1024 || threads() > 4);
            blocked_rows(out, 2 * whole, dots[0], two, r2, r, fmt.chunk, rs);
        }
        blocked_rows(out, 2 * whole, dots[0], Expr(), rt, r, fmt.chunk, rs);
        return;
    }
    pairs.busy = rows.busy = rest;
    pairs.interleave = rows.interleave = k.interleave;
    // Staged tables: no row pairs. Rows over 11 KB in single rows (q8_0 gemv
    // at K = 14336 1.15x on one thread).
    Expr paired = fits(pairs, 1);
    if (fmt.row_bytes.defined()) paired = paired && fmt.row_bytes <= 11 * 1024;
    if (k.row_pairs && staged_tables(rs).empty()) blocked(out, 2 * whole, dots[0], paired, pairs, r, integer, rs, t, true, fmt.sub, fmt.act_sub, k, true);
    blocked(out, 2 * whole, dots[0], Expr(), rows, r, integer, rs, t, true, fmt.sub, fmt.act_sub, k, true);
}

}  // namespace ggml
