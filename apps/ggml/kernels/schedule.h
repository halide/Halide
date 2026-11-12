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

// The call of rs's that defines f, if any.
inline const ApproximationTraceNode *defining_call(const std::vector<ApproximationResult> &rs, const Func &f) {
    for (const ApproximationResult &r : rs) {
        if (const ApproximationTraceNode *n = r.defining_call(f)) return n;
    }
    return nullptr;
}

// The Tabulate (schemes/common.h) whose decode in r defines f: its lut (a
// constant of (element, index)) if `lut`, else the decode reading it (with
// `precomputed`, only if it reads it precomputed: Func::precompute()).
inline const Tabulate *tabulate(const ApproximationResult &r, const Func &f, bool lut, bool precomputed = false) {
    const ApproximationTraceNode *n = r.defining_call(f);
    const Tabulate *t = n ? n->stage.target<Tabulate>() : nullptr;
    if (!t || !has(lut ? n->intermediates : n->ports, f.name())) return nullptr;
    return !precomputed || n->intermediates[0].is_precomputed() ? t : nullptr;
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

// Per bit of the index of r.x's (a block's `block` values) sub-blocks of `sub`
// values (a power of 2), how many of dot's loads varying with r.x's bits below `low` (0: with any)
// it leaves in place: sub-blocks differing only in it read the same bytes
// (sub = 1: q4_0's bit 4, a byte's two nibbles; q5_K's sub-blocks: qh's bits).
inline std::vector<int> shared_bits(Func dot, const RDom &r, int block, int sub = 1, int low = 0) {
    int bits = 0, s = 0;
    while ((1 << bits) < block)
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

// Are the weight's codes decoded from a field (q4_0's nibbles), not read as
// stored (q8_0's)?
inline bool decoded_codes(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> cc = rs[0].decoded_components(tag::codes), st = rs[0].stored_components(tag::codes);
    return cc.size() == 1 && std::any_of(st.begin(), st.end(), [&](const Func &f) { return f.type() != cc[0].type(); });
}

// May a gemv tile stream two weight rows (or records) `d` bytes apart, where
// one already streams from DRAM at about a core's rate for two (codes read
// as stored, q8_0's; records)? On M3 a core streams two rows 12-56 KB apart
// at 0.6-0.85x one row's rate (closer or farther, at 1-1.3x; from cache, at
// 1.4x): the hardware prefetcher's (the distance virtual, not physical: not
// DRAM's pages or banks, nor cache sets), so such pairs are 1.1-1.3x slower
// on one thread (q8_0 at K >= 16384, two q4_0.i4.4x4 records at K = 8192).
// Decoded codes stream at 20-30 GB/s, under the two rows' rate: their pairs
// gain at any distance. M3's window: (11 KB, 64 KB).
inline Expr two_streams(const Expr &d) {
    return d <= 11 * 1024 || d >= 64 * 1024;
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
    // plain f32 weights: no scheme, so rs holds no approximation of their
    // storage (rs[0] is not the weight's)
    bool wplain = false;
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
    bool row_pairs = true, index_words = false, plain_index_words = true, cuda_one_warp = false, code_words = false;
    std::string int_tiles = "mmm";
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
    s.reorder(ni, mi, mo, no);
    if ((ts.nt > 16 && ts.mt >= 8) || ts.mt > 32) {
        // (a macro tile, blocked_cols(): its copy in 16 x 8 pieces; not a
        // wide gemv tile, one row in m)
        VarOrRVar mii = row_var(out, st, "mii");
        Var nii("nii");
        s.split(ni, ni, nii, 16).split(mi, mi, mii, 8).reorder(nii, mii, ni, mi).vectorize(nii).unroll(mii);
    } else {
        s.unroll(mi);
        ts.nt > 1 ? s.vectorize(ni) : s.unroll(ni);
    }
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
inline bool register_lut(const std::vector<ApproximationResult> &rs, const Func &lut, const Target &target = Target()) {
    const Tabulate *t = tabulate(rs, lut, true);
    return t && (t->per * t->entries * lut.type().bytes() <= 16 || (target.arch == Target::X86 && t->entries == (1 << t->per)));
}

inline void luts(const std::vector<ApproximationResult> &rs, bool tables, const Target &target = Target()) {
    std::set<std::string> seen;
    for (const ApproximationResult &r : rs) {
        for (Func f : r.decode_funcs()) {
            const Tabulate *t = tabulate(r, f, true);
            if (!t || !seen.insert(f.name()).second) continue;
            if (t->data || (tables && !register_lut(rs, f, target))) f.precompute();
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

// The elements of the unsigned struct field array that f reads as it is
// (StructLayout's decode: iq3_xxs's grid index bytes, q5's qh: 4), else 0.
inline int array_field(const std::vector<ApproximationResult> &rs, const Func &f) {
    const ApproximationTraceNode *n = defining_call(rs, f);
    const StructLayout *l = n ? n->stage.target<StructLayout>() : nullptr;
    for (size_t i = 0; l && i < n->ports.size(); i++) {
        if (n->ports[i].name() != f.name()) continue;
        const StructField &s = l->port_field(i);
        return s.type.is_uint() ? s.array_extent.value_or(0) : 0;
    }
    return 0;
}

// A table decode g's `index` (its Tabulate's input), if read as is (iq3_xxs's
// grid index bytes), else, if `nested`, the bytes read as is that it is
// computed from (iq3_s's qs): staged at `at`, a word per load, in GPRs (MemoryType::GPR), each index
// a shift and a mask; in place, each is a load (iq3_xxs gemv 1.16-1.27x on
// M3). Not if !plain, the index read as is in place (x86: a byte load each).
inline void staged_indices(const std::vector<ApproximationResult> &rs, Func g, Func index, LoopLevel at, const Target &t, bool nested, bool plain = true) {
    std::vector<std::pair<Func, Func>> staged;  // (Func read as is, its caller)
    if (array_field(rs, index)) {
        if (plain) staged.emplace_back(index, g);
    } else if (const ApproximationTraceNode *n = nested ? defining_call(rs, index) : nullptr) {
        // (g's own copy: the index is shared by the tables' copies for
        // other consumers, e.g. mul_mat's other tiles)
        std::vector<Func> fs;
        for (const Func &f : n->inputs) {
            // (bytes, a word of them per load; not iq3_xxs's u32 sign
            // words, two to a word: no fewer loads)
            if (array_field(rs, f) && 4 * f.type().bits() <= t.bits) fs.push_back(f);
        }
        if (!fs.empty()) {
            Func own = index.clone_in(g);
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
        if (int n = array_field(rs, f); n < lanes) {
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
inline void tables(const std::vector<ApproximationResult> &rs, const std::vector<Func> &consumers, LoopLevel at, const Target &t, bool nested = false, bool plain = true) {
    for (Func f : staged_tables(rs)) {
        Func g = f.clone_in_callers(consumers);
        if (!g.defined()) continue;
        Var j = g.args()[0], jo("jo"), ji("ji");
        g.compute_at(at).split(j, jo, ji, tabulate(rs, f, false)->per).vectorize(ji).unroll(jo);
        for (size_t k = 1; k < g.args().size(); k++) {
            g.unroll(g.args()[k]);
        }
        staged_indices(rs, g, defining_call(rs, f)->inputs[0], at, t, nested, plain);
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

// The weight's code decoders that add a constant code offset (an affine
// map's offset_term(): tq2_0's field - 1, q6_K's high bits - 32; not q4_0's,
// inside its narrowing cast) and the linear ones that add their values
// (q6_K's codes), if the codes the dot reads are among them (not iq2_s's),
// else none. Eager-inlined, each constant offset reaches the widening cast of
// the codes' sum, which hoist_invariants() then splits where it is exact.
inline std::vector<Func> offset_path(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> path;
    for (Func f : decoders(rs, only({tag::codes}), 0, 1)) {
        const ApproximationTraceNode *n = rs[0].defining_call(f);
        const Pointwise *p = n ? n->stage.target<Pointwise>() : nullptr;
        bool offset = p && p->affine_map() && p->affine_map()->offset_term();
        // (decoders are in order: a stage's inputs precede it)
        if (offset || (n && linear(n->stage) &&
                       std::any_of(n->inputs.begin(), n->inputs.end(), [&](const Func &g) { return has(path, g.name()); }))) {
            path.push_back(f);
        }
    }
    // the codes the dot reads
    for (Func f : rs[0].decoded_components(tag::codes)) {
        if (has(path, f.name())) return path;
    }
    return {};
}

// The codes the dot reads (offset_path()'s) split as offset + unsigned, to
// stage the unsigned ones: {offset + u, u} (u decoded from the path's inputs,
// the offset cancelled), dot's calls of the codes (once it calls them
// directly) redirected to the former; or none.
// With `bias`, codes without that form (the iq formats' signed grid values;
// iq1's delta, +-1 by a sign bit) whose bounds (their type's, or promised)
// span at most 256 values are biased to unsigned bytes instead (u = q - min),
// as GGML's tiled gemm stages them.
inline std::pair<Func, Func> offset_codes(Func dot, const std::vector<ApproximationResult> &rs, bool bias = false) {
    std::vector<Func> path = offset_path(rs), qs = rs[0].decoded_components(tag::codes);
    Func q;
    for (Func f : qs) {
        if (has(path, f.name())) q = f;
    }
    if (path.empty() && bias && qs.size() == 1) q = qs[0];
    auto calls = Internal::find_direct_calls(dot.function());
    if (!q.defined()) return {};
    // (redirected already, by an earlier tile's call: the same pair)
    for (const auto &[name, f] : calls) {
        if (name.rfind(q.name() + "_offset", 0)) continue;
        for (const auto &[un, uf] : Internal::find_direct_calls(f)) {
            if (!un.rfind(q.name() + "_unsigned", 0)) return {Func(f), Func(uf)};
        }
    }
    if (!calls.count(q.name())) return {};
    std::vector<Var> as = q.args();
    std::vector<Expr> es(as.begin(), as.end());
    Func full(Internal::unique_name(q.name() + "_full"));
    full(as) = q(es);
    // (q's definition inlined: u calls the tables, cloned per tile)
    full.eager_inline(path.empty() ? std::vector<Func>{q} : path);
    Expr e = simplify(full.value());
    Func u(Internal::unique_name(q.name() + "_unsigned")), o(Internal::unique_name(q.name() + "_offset"));
    auto lower = [](const Expr &x) {
        Internal::Interval b = Internal::bounds_of_expr_in_scope(x, Internal::Scope<Internal::Interval>());
        return b.has_lower_bound() ? Internal::as_const_int(simplify(b.min)) : std::nullopt;
    };
    const auto *a = e.as<Internal::Add>();
    // (not where the rest is signed, iq1's 8 g - 2 s + 1: biased instead)
    auto al = a ? lower(a->a) : std::nullopt;
    if (a && Internal::is_const(a->b) && !(bias && al && *al < 0)) {
        u(as) = a->a;
        o(as) = u(es) + a->b;
    } else {
        if (!bias || !q.type().is_int()) return {};
        Internal::Interval b = Internal::bounds_of_expr_in_scope(e, Internal::Scope<Internal::Interval>());
        auto lo = b.has_lower_bound() ? Internal::as_const_int(simplify(b.min)) : std::nullopt;
        auto hi = b.has_upper_bound() ? Internal::as_const_int(simplify(b.max)) : std::nullopt;
        if (!lo || !hi || *hi - *lo > 255 || *lo >= 0) return {};
        u(as) = cast<uint8_t>(cast(Int(32), e) - (int)*lo);
        o(as) = cast<int16_t>(u(es)) + cast<int16_t>((int)*lo);
    }
    dot.function().substitute_calls(calls.at(q.name()), o.function());
    return {o, u};
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

// The weight's decoded per-sub-block scales (and mins): its decoded scale
// components with the values' dimensions.
inline std::vector<Func> scale_decoders(const std::vector<ApproximationResult> &rs) {
    std::vector<Func> sc = rs[0].decoded_components(tag::scale), fs;
    std::copy_if(sc.begin(), sc.end(), std::back_inserter(fs), [](const Func &f) { return f.dimensions() == 3; });
    return fs;
}

// The weight's per-sub-block scale decoders whose stored loads are shared
// across sub-blocks (some, not all, sub-block index bits load-invariant): in
// place, unpacking their bit fields inside the step loop repeats the loads and
// shifts at every step. Staged per block: vectorized across the sub-blocks when
// adjacent sub-blocks share a load (low bit invariant), or unrolled by the
// bit-field index over contiguous load runs of `1 << ctz(inv)`.
inline std::vector<std::pair<Func, int>> packed_sub_scales(const std::vector<ApproximationResult> &rs, int block, int sub, int step) {
    if (!sub || sub >= step) return {};
    int bits = 0;
    while ((1 << bits) < block / sub)
        bits++;
    uint64_t all = (1 << bits) - 1;
    std::vector<std::pair<Func, int>> fs;
    for (Func f : scale_decoders(rs)) {
        uint64_t inv = 0;
        for (uint64_t m : Internal::load_invariant_bits(f.function().definition(), f.args()[0].name(), bits)) {
            if ((~m & all) && (m & all)) inv |= m & all;
        }
        if (inv) {
            int ctz = 0;
            while (!((inv >> ctz) & 1))
                ctz++;
            fs.emplace_back(f, ctz ? (1 << ctz) : (block / sub));
        }
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
    Func sums, blk, sum, part, pair, with;  // (with: the codes' terms, s's Func)
    bool sub_sums = false;                  // d.sum sums the act's sub-block sums (no part)
    bool offsets = false;                   // a constant code offset's (offset_path())
    int offset_lanes = 8;                   // (offsets: d.part's vector)
    Func unsigned_codes;                    // (offsets, split: offset_codes()'s)
};
// `owner` (s's Func): a constant code offset folded with the codes split
// (offset_codes()), the unsigned ones left a Func (d.unsigned_codes).
inline bool derived_sums(Func out, int st, Stage &s, const RDom &r, const std::vector<ApproximationResult> &rs, Sums &d, int act_sub = 0, int sub = 0, const std::vector<Func> &kept = {}, bool fold = true,
                         Func owner = Func(), bool sum_lanes = false) {
    d = Sums();
    if (rs.size() < 2 || rs[1].derived_ports().empty()) return false;
    std::vector<Func> dec = without(decoders(rs, all_but({tag::codes}), 0, 1), kept);
    std::vector<Func> path = fold ? offset_path(rs) : std::vector<Func>{};
    std::vector<Func> group = decoders(rs, only({tag::codes}), 0, 1);
    bool offsets = !path.empty();
    // (no offset path: the codes biased unsigned where owner's, not affine)
    bool bias = fold && owner.defined() && !affine(rs);
    if (!offsets && !bias && !affine(rs)) return false;
    d.offsets = offsets || bias;
    s.eager_inline(dec);
    if ((offsets || bias) && owner.defined()) {
        auto [o, u] = offset_codes(owner, rs, bias);
        if (!offsets && !o.defined()) {
            d = Sums();
            return false;
        }
        offsets = true;
        if (o.defined()) {
            path = {o};
            group = {u};
            d.unsigned_codes = u;
        }
    }
    if (offsets) s.eager_inline(path);
    FuncVec parts = s.hoist_invariants({group});
    if (!parts[1].defined() && offsets) {  // no exact split (iq1's delta): no fold
        d = Sums();
        return false;
    }
    if (!parts[1].defined()) throw std::invalid_argument("derived_sums: no terms free of the codes in " + s.name());
    Func with = parts[0];
    d.sums = parts[1];
    RVar rtl("rtl"), rxt("rxt"), rxj("rxj"), rxh("rxh"), rxa("rxa");
    bool pairs = sub > act_sub;
    if (act_sub && sum_lanes) {
        Stage ss = d.sums.update();
        if (pairs) {
            ss.split(r.x, rxa, rxj, act_sub).split(rxa, rxt, rxh, sub / act_sub);
        } else {
            ss.split(r.x, rxt, rxj, act_sub);
        }
        ss.split(rxt, rtl, rxt, 2);
        d.blk = ss.rfactor({{rtl, Var("tl")}, {r.y, Var("u")}});
    } else {
        d.blk = d.sums.update().rfactor(r.y, Var("u"));
    }
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
        Stage ds = d.sum.update();
        if (!sum_lanes) {
            if (pairs) {
                // r.x = (rxt * sub/act_sub + rxh) * act_sub + rxj, as the port's
                // definition indexes its sub-blocks (matched structurally)
                ds.split(r.x, rxa, rxj, act_sub).split(rxa, rxt, rxh, sub / act_sub);
            } else {
                ds.split(r.x, rxt, rxj, act_sub);
            }
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
        if (!factor || sum_lanes) {
            if (factor) d.sum.update().eager_inline({d.part});
            d.part = Func();
            d.sub_sums = true;
        }
    } else if (read_port(d.sum, rs[1].derived_ports(), d.blk)) {
        d.sum = Func();
    }
    with.compute_at(tile_at(out, st));
    s = with.update();
    d.with = with;
    return true;
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

// d's sums alongside acc's update (at ryo): on gemm tiles vectorized across
// the rows (n), on gemv tiles one accumulator per interleaved block.
// `across_rows`: the sub-block sums vectorized across a record's rows too.
// `act_records`: the act also in whole records.
inline void schedule_sums(Sums d, Func out, int st, Func acc, Var n, Var m, const Tiles &ts, const RDom &r, bool gemm, int vec, bool across_rows = false, bool act_records = false, bool vec_sums = false) {
    if (!d.sums.defined()) return;
    Var u("u"), v("v"), tl("tl");
    RVar ry = r.y, ryo("ryo"), ryi("ryi"), rtl("rtl");
    bool sum_lanes = d.blk.dimensions() > 3;
    LoopLevel tile = tile_at(out, st), blocks(acc, ryo);
    d.sums.compute_at(tile);
    d.sums.update().split(ry, ryo, ryi, ts.interleave, TailStrategy::GuardWithIf);
    if (gemm) {
        d.sums.vectorize(n).unroll(m);
        d.sums.update().reorder(n, m, ryi, ryo).vectorize(n).unroll(m).unroll(ryi).compute_with(acc.update(), ryo);
        d.blk.vectorize_all_stages(n).unroll_all_stages(u, m);
    } else if (sum_lanes) {
        Func sacc = sum_tree(d.sums.update(), {{{rtl, tl}}}, tile, n, m, ryo)[0];
        sacc.update().eager_inline({d.blk}).reorder(tl, ryi, n, m, ryo).unroll(ryi).compute_with(acc.update(), ryo);
        d.sums.update().atomic().vectorize(rtl).unroll(n).unroll(m);
    } else if (vec >= 32 && ts.interleave > 1 && vec_sums && !d.part.defined() && !d.sub_sums && !d.sum.defined()) {
        // Without LUTs on Port 5 (q4_1), vectorizing the interleaved blocks'
        // f16 min*sum conversions (vcvtph2psx + vfmadd231ps) shifts uops off
        // Ports 0/1 onto Port 5 (vec_dot 0.83x -> 0.95x at K=4096, 1.11x at
        // K=14336).
        Func spart = d.sums.update().rfactor(ryi, v);
        d.sums.update().atomic().vectorize(ryi).unroll(n).unroll(m);
        spart.compute_at(tile).reorder_storage(v, n, m).vectorize_all_stages(v).unroll_all_stages(n, m);
        spart.update().eager_inline({d.blk}).reorder(v, n, m, ryo).compute_with(acc.update(), ryo);
        return;
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
        if (sum_lanes) {
            d.sum.reorder_storage(tl, u, n, m).vectorize(tl);
            d.sum.update().atomic().vectorize(rxt).vectorize(tl).unroll(u).unroll(n).unroll(m);
            if (d.pair.defined()) {
                Var t = d.pair.args().back(), tf("tf");
                RVar rxh("rxh");
                d.pair.compute_at(blocks).reorder_storage(t, tl, u, n, m).fuse(t, tl, tf).vectorize(tf).unroll(u).unroll(n).unroll(m);
                d.pair.update().fuse(t, tl, tf).reorder(tf, rxh).vectorize(tf).unroll(rxh).unroll(u).unroll(n).unroll(m);
            }
            return;
        }
        // (x86: at the loop's width, not a zmm of 16 sums in a ymm loop,
        // which takes port 1's vector ALUs offline)
        if (vec >= 32) {
            RVar rto("rto"), rti("rti");
            d.sum.update().atomic().split(rxt, rto, rti, vec / 4).vectorize(rti).unroll(rto).unroll(u).unroll(n).unroll(m);
        } else {
            d.sum.update().atomic().vectorize(rxt).unroll(u).unroll(n).unroll(m);
        }
        if (d.sub_sums) return;
        Var t = d.part.args().back(), to("to"), ti("ti");
        d.part.compute_at(blocks).reorder_storage(t, u, n, m);
        if (d.offsets) {
            // q6_K's int8 scales times the act's int16 sums, 8 lanes a step
            // (sshll, smull; at 16 LLVM shuffles the sign extend: q6_K's
            // vec_dot and one-thread gemv 5-6% slower on M3), on x86 16 (a
            // ymm pmaddwd)
            d.part.split(t, to, ti, d.offset_lanes);
            d.part.update().split(t, to, ti, d.offset_lanes);
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
inline void blocked(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int block, bool integer,
                    const std::vector<ApproximationResult> &rs, const Target &t, bool derived = false,
                    int sub = 0, int act_sub = 0, const Knobs &k = Knobs(), bool gemv = false, int wper = 1) {
    bool sublanes = gemv && k.sublanes, gpr_scales = gemv && k.gpr_scales, defer = gemv, index_words = !gemv && k.index_words;
    bool fold_offsets = gemv ? k.fold != Fold::None : k.fold == Fold::All;
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), u("u"), v("v"), bacc("bacc");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rxo("rxo"), rxi("rxi"), ryo("ryo"), ryi("ryi");
    // lanes of 16 bytes (NEON's) on every target: wider (AVX-512's 64) leaves
    // a 32-value block or sub-block one step, no loop for its scale to hoist
    // out of. With VNNI, integer blocks without sub-blocks, or with 32-value
    // ones, take 32 (GGML's AVX step: a block's or sub-block's dot one ymm
    // vpdpbusd; q4_0/q8_0/iq4_nl vec_dot 1.27-1.37x of 16). Not across
    // sublanes (NEON's shape), nor 16-value sub-blocks (no fewer dots, more
    // shuffles).
    bool vnni = t.has_feature(Target::AVXVNNI) || t.has_feature(Target::AVX512_Zen4);
    int wsub = sub;
    int wide = integer && vnni && (sub % 32 == 0 || float_sub_scales(rs)) && !(sub && sublanes) ? 32 : 16;
    // With AVX-512 VNNI, 16-value sub-blocks decoded without tables take a
    // zmm (any sublanes too): four sub-blocks a step, each scale in its
    // lanes' products, which CodeGen_X86 sums as GGML's AVX2 q2_K/q6_K do
    // (pmaddubsw, then pmaddwd by the scales; one-thread vec_dot 0.45 ->
    // 0.77x of GGML for q2_K, 0.48 -> 0.79x for q6_K). Not tables' (iq2_xs:
    // signed codes, 5% more), nor with offsets not to fold (q3_K's: its
    // one-thread gemv 0.78 -> 0.51x of GGML); in gemm tiles too (q6_K's
    // M = 4..7 1.1x faster than two a step)
    if (integer && vnni && t.natural_vector_size<int8_t>() >= 64 && sub == 16 && !float_sub_scales(rs) &&
        staged_tables(rs).empty() && k.fold != Fold::None) {
        wide = gemv ? 32 : 64;
        sub = 0;
    }
    int per = integer ? 4 : 1, vec = std::min(t.natural_vector_size<int8_t>(), wide);
    if (sub * per <= vec || block <= vec) sub = 0;  // a sub-block per lane step, or one step per block: no sum across sub-blocks
    // With VNNI, signed codes (no offset term: q3_K's, the iq formats') keep
    // the sub-block scales in their lanes' products, at GGML's AVX step of
    // 32: CodeGen_X86 sums them as GGML's AVX2 does (|codes| x signed acts
    // by pmaddubsw, then pmaddwd by the scales) in place of a vpdpbusd and
    // a vpmulld per sub-block (one-thread vec_dot: iq2_xs 0.82 -> 0.96x of
    // GGML, iq4_xs 1.0 -> 1.2x, q3_K 0.73 -> 0.79x). Not unsigned ones'
    // (q4_K's, affine), whose dot vpdpbusd takes as is (0.91 -> 0.77x)
    if (gemv && integer && vnni && sub && sub <= 32 && !float_sub_scales(rs) && !affine(rs)) {
        wide = vec = std::min(t.natural_vector_size<int8_t>(), 32);
        sub = 0;
    }
    int nsub = sub ? block / sub : 0;
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
    std::vector<std::pair<Func, int>> sdec;
    if (!sl && gpr_scales) {
        gpr = scale_decoders(rs);
        dkept.insert(dkept.end(), gpr.begin(), gpr.end());
    } else if (!sub && t.arch == Target::X86) {
        sdec = packed_sub_scales(rs, block, wsub, vec);
        if (gemv && sdec.empty() && float_sub_scales(rs) && wsub > per) {
            for (Func f : scale_decoders(rs))
                sdec.emplace_back(f, 1);
        }
        for (auto &[f, _] : sdec)
            dkept.push_back(f);
    }
    if (derived && derived_sums(out, st, s, r, rs, d, act_sub, sub > act_sub ? sub : 0, dkept, fold_offsets, Func(), t.arch == Target::X86 && ts.nt <= 2)) aff = false;
    if (d.sum.defined() && !sdec.empty()) {
        for (auto &[f, _] : sdec)
            d.sum.update().eager_inline({f});
    }
    if (t.arch == Target::X86) d.offset_lanes = 16;
    if (aff) kept.clear();
    RVar rxl("rxl");  // (sl: the sub-block's lane)
    if (sl) {
        s.split(r.x, rxs, rxc, sub).split(rxs, rxs, rxl, vec / per).split(rxc, rxc, rxo, vec);
    } else {
        sub ? s.split(r.x, rxs, rxc, sub).split(rxc, rxc, rxo, vec) : s.split(r.x, rxc, rxo, vec);
    }
    s.split(rxo, rxo, rxi, per);
    RVar rl = sl ? rxl : rxo, ryw("ryw"), ryp("ryp");
    Var w("w");
    Func wblk, win;
    Stage bs = s;
    if (wper > 1) {
        s.split(ry, ryw, ryp, wper);
        wblk = s.rfactor({{rl, lane}, {ryw, w}});
        wblk.bound(lane, 0, vec / per);
        wblk.update().eager_inline(decoders(rs, all_but({tag::codes}), 0, 1));
        win = wblk.update().hoist_invariants()[0];
        win.bound(lane, 0, vec / per);
        s.eager_inline({wblk});
        bs = win.update();
    }
    Func blk = bs.rfactor(wper > 1 ? std::vector<std::pair<RVar, Var>>{{ryp, u}} : std::vector<std::pair<RVar, Var>>{{rl, lane}, {ry, u}});
    blk.bound(lane, 0, vec / per);  // the lane's range, so the block's scales hoist
    // float sub-block scales (nvfp4's): the codes' dot is integer per
    // sub-block only
    bool fsub = sub && integer && float_sub_scales(rs);
    std::vector<Func> bkept = kept;
    bkept.insert(bkept.end(), gpr.begin(), gpr.end());
    for (auto &[f, _] : sdec)
        bkept.push_back(f);
    Func rest, mid, codes = block_dot(blk, rs, integer && !fsub, rest, mid, aff, bkept);
    if (win.defined()) win.update().eager_inline({blk});
    // per sub-block (v): its scale times its codes' dot (inner)
    std::vector<std::tuple<Func, Func, Func>> subs;
    for (Func f : {codes, rest}) {
        if (!sub || !f.defined()) continue;
        Func part = f.update().rfactor(rxs, v);
        part.bound(lane, 0, vec / per);
        Func inner = part.update().hoist_invariants()[0];
        subs.emplace_back(f, part, fsub ? inner.change_type(Int(32)) : inner);
    }
    int table_loads = 0;
    for (Func tf : staged_tables(rs)) {
        table_loads += vec / tabulate(rs, tf, false)->per;
    }
    bool chunk_tables = !sub && t.arch == Target::X86 && block > vec && codes.defined() && table_loads > 0;
    int il = wper > 1 || chunk_tables ? 1 : ts.interleave;
    s.split(wper > 1 ? ryw : ry, ryo, ryi, il, TailStrategy::GuardWithIf);
    Func acc = sum_tree(s, {{{rl, lane}, {ryi, bacc}}, {{rl, lane}}}, tile_at(out, st), n, m, ryo)[0];
    s.atomic().vectorize(rl).unroll(n).unroll(m);
    if (win.defined()) {
        win.compute_at(acc, ryo).reorder_storage(lane, n, m).bound_storage(w, il).vectorize_all_stages(lane).unroll_all_stages(w, n, m);
        win.update().unroll(ryp);
    }
    // rfactor put the lanes last in storage; bound_storage keeps the K
    // tail's blocks on the stack
    for (Func f : {win.defined() ? Func() : blk, mid, codes}) {
        if (!f.defined()) continue;
        f.compute_at(acc, ryo).reorder_storage(lane, n, m).bound_storage(u, wper > 1 ? wper : il).vectorize_all_stages(lane).unroll_all_stages(u, n, m);
        if (wper > 1) f.bound_storage(w, il).unroll_all_stages(w);
    }
    if (rest.defined()) {
        // the act's block sum, once per act column (n a broadcast)
        rest.compute_at(acc, ryo).reorder_storage(lane, n, m).bound_storage(u, wper > 1 ? wper : il).vectorize_all_stages(lane).unroll_all_stages(u, m);
        if (wper > 1) rest.bound_storage(w, il).unroll_all_stages(w);
        ts.nt > 1 ? rest.vectorize_all_stages(n) : rest.unroll_all_stages(n);
    }
    RVar rxco("rxco"), rxci("rxci");
    bool split_chunks = chunk_tables && table_loads > 8 && sdec.empty();
    for (Func f : {codes, rest}) {
        if (!f.defined() || sub) continue;
        if (f.name() == codes.name() && sdec.empty() && defer && integer && wper == 1 && hoisted_shifts(codes, rxc, block / vec, vec / per, per, LoopLevel(acc, ryo), ts.interleave, n, m)) continue;
        if (split_chunks && f.name() == codes.name()) {
            f.update().reorder(rxi, lane, n, m, rxc, u).split(rxc, rxco, rxci, 2).atomic().vectorize(rxi).unroll(rxci);
            continue;
        }
        f.update().atomic().vectorize(rxi).unroll(rxc);
    }
    // per block and sub-block, the tile's sub-block dots (inner), then their
    // scales (part)
    std::vector<Func> readers{win, blk, mid, codes, rest};
    // With VNNI, sub-blocks of a vec's quads in pairs, their dots and scales
    // one vector of both (GGML's AVX2 q6_K): half the dots, multiplies and
    // adds, the scales loaded in pairs. 16-value sub-blocks only: pairs of
    // 32-value ones (a zmm) lose (q4_K/q5_K/iq4_xs vec_dot 0.76-0.85x)
    int pair = vnni && !sl && !fsub && vec * 2 <= std::min(t.natural_vector_size<int8_t>(), 32) && nsub % 2 == 0 ? 2 : 1;
    for (auto [f, part, inner] : subs) {
        readers.insert(readers.end(), {part, inner});
        RVar rso("rso"), rsi("rsi");
        if (pair > 1) {
            // the block's accumulator too a vector of both (one fold per block)
            Var w("w");
            f.update().split(rxs, rso, rsi, pair);
            Func fp = f.update().rfactor(rsi, w);
            fp.compute_at(acc, ryo).reorder_storage(lane, w, n, m).vectorize_all_stages(lane).vectorize_all_stages(w).unroll_all_stages(u, n, m);
            fp.update().reorder(lane, w, n, m, rso, u).unroll(rso);
            f.update().unroll(rsi);
            readers.push_back(fp);
            for (Func g : {part, inner}) {
                g.compute_at(fp, rso).reorder_storage(lane, v, n, m).vectorize_all_stages(lane).vectorize_all_stages(v).unroll_all_stages(u, n, m);
            }
        } else {
            f.update().reorder(lane, n, m, rxs, u).unroll(rxs);
        }
        for (Func g : {part, inner}) {
            if (pair > 1) break;
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
    bool vec_sums = true;
    for (const Func &f : rs[0].decode_funcs()) {
        vec_sums &= !tabulate(rs[0], f, true);
    }
    schedule_sums(d, out, st, acc, n, m, ts, r, ts.nt > 2, vec, false, false, vec_sums);
    // gpr_scales: the scales and mins decoded per block, 4 bytes a word in a
    // GPR (q4_K vec_dot +7-12%)
    std::vector<Func> sum_readers;
    if (d.part.defined()) sum_readers.push_back(d.part);
    else if (d.sub_sums && d.sum.defined()) sum_readers.push_back(d.sum);
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
        for (Func k : kept) {
            if (!calls(part, k)) continue;
            k.compute_at(acc, ryo).vectorize(k.args()[0], vec);
            readers.push_back(k);
        }
    }
    for (auto &[k, run] : sdec) {
        if (!codes.defined()) continue;
        Func w = k.in_callers({codes, blk});
        if (!w.defined()) continue;
        Var kw("kw"), kb("kb");
        w.compute_at(acc, ryo);
        run == 1 ? w.unroll(w.args()[0]) : run < block / wsub ? w.split(w.args()[0], kw, kb, run).unroll(kw).vectorize(kb, run, VectorizeIn::SWAR) : w.vectorize(w.args()[0]);
        for (size_t i = 1; i < w.args().size(); i++)
            w.unroll(w.args()[i]);
        readers.push_back(w);
    }
    tables(rs, readers, chunk_tables ? LoopLevel(codes, split_chunks ? rxci : rxc) : LoopLevel(acc, ryo), t, index_words, k.plain_index_words);
}

// bf16 weights and acts, with a bf16 dot (AVX512-BF16's vdpbf16ps, GGML's
// AVX-512 bf16 vec_dot): flat()'s products summed in pairs.
inline bool bf16_pairs(const std::vector<ApproximationResult> &rs, const Target &t) {
    bool pairs = t.with_implied_features().has_feature(Target::AVX512_Zen4) && rs.size() == 2;
    for (const ApproximationResult &a : rs) {
        pairs = pairs && a.encoded.size() == 1 && a.encoded[0].type() == BFloat(16);
    }
    return pairs;
}

// Float weights (one value per record): per output, f32 products in vectors
// of `vec` values of r.y, ts.interleave independent accumulators (the K tail
// guarded), then summed across them. With `pairs`, each lane sums two
// adjacent products first (a 2-way dot product instruction's shape).
inline void flat(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, const Target &t, bool pairs = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], lane("lane"), bacc("bacc");
    RVar ry = r.y, ryo("ryo"), ryi("ryi"), ryl("ryl"), rp("rp");
    int vec = 2 * t.natural_vector_size<float>(), p = pairs ? 2 : 1;
    s.split(ry, ryo, ryi, vec * p * ts.interleave, TailStrategy::GuardWithIf).split(ryi, ryi, ryl, vec * p);
    if (pairs) s.split(ryl, ryl, rp, 2);
    std::vector<Func> fs = sum_tree(s, {{{ryl, lane}, {ryi, bacc}}, {{ryl, lane}}}, tile_at(out, st), n, m, ryo);
    if (pairs) fs[0].update().atomic().vectorize(rp).vectorize(lane);
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
inline void blocked_rows(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int block, int chunk,
                         const std::vector<ApproximationResult> &rs, int group = 0, int sub = 0, int act_sub = 0) {
    std::vector<int> shared = sub ? shared_bits(dot, r, block, sub) : std::vector<int>{};  // (dot as defined)
    Stage s = tile_out(out, st, dot, cond, ts, r, true);
    Var n = dot.args()[0], m = dot.args()[1], q("q"), u("u"), v("v"), bacc("bacc"), mo("mog"), mi("mig");
    RVar ry = r.y, rx = r.x, rxs("rxs"), rxc("rxc"), rp("rp"), rq("rq"), ri("ri"), ryo("ryo"), ryi("ryi");
    Sums d;
    if (affine(rs) && !derived_sums(out, st, s, r, rs, d, act_sub, sub)) throw std::invalid_argument("blocked_rows: affine weights need an act with derived sums");
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
        int g = sharing_group(shared, 2, block / sub);
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
inline void blocked_mmla(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int block,
                         const std::vector<ApproximationResult> &rs, const Target &t, bool derived = false, int sub = 0, int act_sub = 0, bool records = false, bool act_records = false,
                         int per = 1, const Knobs &k = Knobs()) {
    bool fold_offsets = k.fold == Fold::All, gpr_scales = k.gpr_scales, index_words = k.index_words;
    std::vector<int> shared = sub ? shared_bits(dot, r, block, sub) : std::vector<int>{};  // (dot as defined)
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
    RVar rxp("rxp");
    Var vp("vp");
    int period = 0;
    if (sub && act_records) {
        int g = sharing_group(shared, 2, block / sub);
        bool zero = false, beneath = false;
        for (int b = 0; (1 << b) < g; b++) {
            beneath |= zero && shared[b];
            zero |= !shared[b];
        }
        if (g < block / sub && beneath) period = g * sub;
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
        int subs = block / sub;
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
    tables(rs, {blk, mid, codes, rest, part, inner}, LoopLevel(acc, ryo), t, index_words, k.plain_index_words);
}

// d's sums vectorized across the columns (m, `lanes` a step), as the tile
// accumulators of blocked_cols() and blocked_amx(): per tile, the rows
// unrolled (the act's sub-block sums are the rows' alike: their reads CSE);
// the act's ports read (q8_K's bsums) staged up front, the columns innermost
// (a dense load, not a gather across them). A `macro` tile (under that
// condition): in 8-row strips.
inline void column_sums(Sums d, Func out, int st, Var n, Var m, const RDom &r, int lanes, const std::vector<ApproximationResult> &rs, Expr macro = Expr()) {
    Var smo("smo"), smi("smi"), nb("nb");
    RVar ry = r.y;
    d.sums.compute_at(tile_at(out, st)).reorder_storage(m, n);
    Stage su = d.sums.update();
    su.split(m, smo, smi, lanes);
    if (macro.defined()) {
        d.sums.specialize(macro).split(m, m, smi, lanes).split(n, nb, n, 8).reorder(smi, n, m, nb).vectorize(smi).unroll(n);
        su.specialize(macro).split(n, nb, n, 8).reorder(smi, n, smo, nb, ry).vectorize(smi).unroll(n);
    }
    d.sums.vectorize(m, lanes).unroll(n);
    su.reorder(smi, n, smo, ry).vectorize(smi).unroll(n);
    std::vector<Func> sums;
    for (Func f : {d.blk, d.sum, d.part, d.pair}) {
        if (!f.defined()) continue;
        sums.push_back(f);
        f.compute_at(d.sums, smo).reorder_storage(m, n).vectorize_all_stages(m);
        std::vector<VarOrRVar> rest;
        for (Var a : f.args()) {
            if (a.name() != m.name()) rest.push_back(a);
        }
        f.unroll_all_stages(rest);
        for (int u = 0; u < f.num_update_definitions(); u++) {
            for (const RVar &x : f.rvars(u)) {
                f.update(u).unroll(x);
            }
        }
    }
    for (const DerivedPort &p : rs[1].derived_ports()) {
        Func g = Func(p.stored).in_callers(sums);
        if (!g.defined()) continue;
        std::vector<Var> as = g.args();
        Var am = as.back();
        std::vector<Var> order{am};
        order.insert(order.end(), as.begin(), as.end() - 1);
        std::vector<VarOrRVar> loops(order.begin(), order.end());
        g.compute_root().reorder_storage(order).reorder(loops).vectorize(am, lanes, TailStrategy::GuardWithIf).parallel(as[as.size() - 2]);
    }
}

// AMX tiles (x86 tdpbssd/tdpbusd), as GGML's AMX backend: out's stage st
// under cond on ts tiles of 16 rows of N, each block's codes' dot one 16 x 16
// int32 tile op per 64 bytes (the act's rows the A tile, the weight's codes
// the B tile, staged in its 4-byte interleave), then scaled into f32
// accumulators. ts.mt / 16 tiles of M share each block's staged weights.
// Sub-blocks of `sub` values (GGML's AMX K-quant kernels): one tile op per
// sub-block, its int32 result times the sub-block's scales (decoded per block,
// a row's at a time), summed per block; the code-free term (mins) per block
// across the columns, as blocked_cols(). `packed`: the weight's records
// already in the B tile's layout (16 rows in 4-byte pieces): its codes
// decoded per block, dense, in place, shared by every sub-block and M tile.
inline void blocked_amx(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int block,
                        const std::vector<ApproximationResult> &rs, const Target &t, int sub = 0, int act_sub = 0, bool packed = false) {
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], u("u"), v("v");
    RVar ry = r.y, rx = r.x, rxs("rxs"), rxc("rxc");
    std::vector<Func> wc = rs[0].decoded_components(tag::codes), ac = rs[1].decoded_components(tag::codes);
    std::vector<Func> keep = wc;
    keep.insert(keep.end(), ac.begin(), ac.end());
    // (sub-blocks: their scales and mins decoded per block, a vector of a
    // row's sub-blocks each, not a byte gather across the rows per sub-block)
    std::vector<Func> sc = sub ? scale_decoders(rs) : std::vector<Func>{};
    keep.insert(keep.end(), sc.begin(), sc.end());
    // an affine weight's code-free term (q4_1's min times the act's sum: its
    // s port), as GGML's AMX acc_C: one f32 product per block and output
    Sums sm;
    Func acc = dot;
    // (sub-blocks: a constant code offset folded, as blocked_cols(); packed,
    // signed codes without one left signed: tdpbssd, no bias to undo, iq4_xs
    // 1.1-1.3x; transposed per sub-block, the bias is 1.06x faster at M = 8)
    bool fold = sub && (!packed || !offset_path(rs).empty());
    if (derived_sums(out, st, s, r, rs, sm, act_sub, sub > act_sub ? sub : 0, keep, sub != 0, fold ? dot : Func())) acc = sm.with;
    if (sm.unsigned_codes.defined()) {
        wc = {sm.unsigned_codes};
        keep.push_back(sm.unsigned_codes);
    }
    if (sub) {
        s.split(rx, rxs, rxc, sub);
        rx = rxc;
    }
    Func blk = s.rfactor(ry, u);
    Func rest, mid, codes = block_dot(blk, rs, true, rest, mid, false, keep), part;
    // the accumulators: ts.mt / 16 tiles of 16 x 16 f32, each block's weights
    // staged once for them all
    VarOrRVar at = ry;
    if (ts.mt > 16) {
        Var mo("mt"), mi("mi");
        s.split(m, mo, mi, 16).reorder(n, mi, mo, ry).vectorize(n).unroll(mi);
        at = mo;
    } else {
        s.reorder(n, m, ry).vectorize(n).unroll(m);
    }
    LoopLevel tile(acc, at);
    if (sm.sums.defined() && !sub) {
        Var smo("smo"), smi("smi");
        sm.sums.compute_at(tile_at(out, st)).vectorize(n).unroll(m);
        sm.sums.update().split(m, smo, smi, 16).reorder(n, smi, smo, ry).vectorize(n).unroll(smi);
        for (Func f : {sm.blk, sm.sum}) {
            if (f.defined()) f.compute_at(sm.sums, smo).vectorize_all_stages(n).unroll_all_stages(m, u);
        }
    } else if (sm.sums.defined()) {
        // sub-blocks: across the columns, as blocked_cols()
        column_sums(sm, out, st, n, m, r, std::min(16, ts.mt), rs);
    }
    blk.compute_at(tile).vectorize_all_stages(n).unroll_all_stages(m);
    if (sub) {  // per sub-block (v): its scales times its codes' dot (the tile op's)
        part = codes.update().rfactor(rxs, v);
        Func inner = part.update().hoist_invariants()[0];
        codes.compute_at(tile).vectorize_all_stages(n).unroll_all_stages(m);
        // (unrolled: each sub-block's bit field positions constant)
        codes.update().reorder(n, m, rxs).unroll(rxs);
        part.compute_at(codes, rxs).vectorize_all_stages(n).unroll_all_stages(m, v);
        tile = LoopLevel(codes, rxs);
        codes = inner;
    }
    // the codes' dot: one tile op per 64 values of the block (K <= 64 bytes)
    RVar rxo("rxo"), rxi("rxi");
    codes.compute_at(tile).store_in(MemoryType::Tile).vectorize(n).vectorize(m);
    Stage cu = codes.update();
    if (sub) {
        cu.reorder(rx, n, m).atomic().vectorize(rx).vectorize(n).vectorize(m);
    } else if (block > 64) {
        cu.split(rx, rxo, rxi, 64).reorder(rxi, n, m, rxo).atomic().vectorize(rxi).vectorize(n).vectorize(m);
    } else {
        cu.reorder(rx, n, m).atomic().vectorize(rx).vectorize(n).vectorize(m);
    }
    Func cs = codes.in(sub ? part : blk);
    cs.compute_at(tile).vectorize(n).vectorize(m);
    // operands staged per block: the weight's codes in AMX's layout
    // (4 values of a row, then the rows, then the next 4), the act's dense
    // (the weight's codes, or their offline staged input: what codes calls
    // but the act's)
    Var ko("ko"), ki("ki");
    // (sub-blocks: decoded per sub-block, a 512-byte transpose; packed
    // weights, already in AMX's layout: per block, dense, no transpose)
    LoopLevel wat = sub && !packed ? tile : LoopLevel(acc, ry);
    std::vector<Func> wops, ds;
    for (const auto &kv : Internal::find_direct_calls(codes.function())) {
        bool act = kv.first == codes.name();
        for (Func f : ac)
            act = act || kv.first == f.name();
        if (!act) wops.emplace_back(kv.second);
    }
    for (Func f : wops) {
        // decoded in rows (dense, vectorized), then transposed in words
        // (a clone with tables: their rows staged for this tile's decode alone)
        Func d = staged_tables(rs).empty() ? f.in(codes) : f.clone_in(codes);
        if (packed) {
            Var k = d.args()[0], b = d.args()[1], row = d.args()[2];
            d.compute_at(wat).split_storage(k, ko, ki, 4).reorder_storage(ki, row, ko, b);
            d.split(k, ko, ki, 4).reorder(ki, row, ko, b).vectorize(ki).vectorize(row).unroll(ko);
            ds.push_back(d);
            continue;
        }
        Func w = d.in(codes);
        Var k = w.args()[0], b = w.args()[1], row = w.args()[2];
        d.compute_at(wat).vectorize(d.args()[0]).unroll(d.args()[2]);
        w.compute_at(wat).split_storage(k, ko, ki, 4).reorder_storage(ki, row, ko, b);
        w.split(k, ko, ki, 4).reorder(ki, row, ko, b).vectorize(ki).vectorize(row).vectorize(ko);
        ds.push_back(d);
    }
    // (q5_0's table rows staged per block: dense loads, not a byte gather)
    tables(rs, ds, wat, t);
    for (Func f : ac) {
        Func a = f.in(codes);
        a.compute_at(tile).vectorize(a.args()[0]).unroll(a.args()[2]);
    }
    std::vector<Func> sums;
    for (Func f : {sm.blk, sm.sum, sm.part, sm.pair}) {
        if (f.defined()) sums.push_back(f);
    }
    for (Func f : sc) {
        // (the mins' own copy where the code-free term reads them)
        std::vector<Func> fs;
        if (Func g = f.clone_in_callers(sums); g.defined()) fs.push_back(g.compute_at(sm.sums, Var("smo")));
        if (Func g = f.clone_in_callers({part}); g.defined()) fs.push_back(g.compute_at(LoopLevel(acc, at)));
        for (Func g : fs) {
            g.vectorize(g.args()[0], block / sub);
            for (size_t i = 1; i < g.args().size(); i++)
                g.unroll(g.args()[i]);
        }
    }
}

// AVX-512 VNNI gemm tiles vectorized across M, as GGML's tiled x86 gemm
// (ggml-cpu/tiled): out's stage st under cond on ts tiles (nt rows, mt
// columns), per block and 16 columns, each row's dot of 4 weight codes
// (broadcast) with the act's 4 codes of 16 columns (a vpdpbusd), the act's
// codes staged up front in that layout (4 codes of a column, then the
// columns, then the next 4), the weight's decoded per block. Sub-blocks'
// scales (and a derived_sums() term) per row and sub-block, broadcast.
inline void blocked_cols(Func out, int st, Func dot, Expr cond, Tiles ts, const RDom &r, int block,
                         const std::vector<ApproximationResult> &rs, const Target &t, int sub = 0, int act_sub = 0, const Knobs &k = Knobs(), bool first = true) {
    const int per = 4, lanes = 16;
    Stage s = tile_out(out, st, dot, cond, ts, r);
    Var n = dot.args()[0], m = dot.args()[1], u("u"), v("v");
    RVar ry = r.y, rxs("rxs"), rxc("rxc"), rxi("rxi");
    std::vector<Func> wc = rs[0].decoded_components(tag::codes), ac = rs[1].decoded_components(tag::codes);
    std::vector<Func> keep = wc;
    keep.insert(keep.end(), ac.begin(), ac.end());
    // (sub-blocks' scales decoded per block, as GGML's tiles: not per row and
    // column group in the sub-block loop)
    std::vector<Func> sc = sub > per ? scale_decoders(rs) : std::vector<Func>{};
    keep.insert(keep.end(), sc.begin(), sc.end());
    Sums d;
    Func acc = dot;
    // a constant code offset always folded (whatever fold_offsets), as
    // GGML's: the unsigned codes staged (q6_K 9.2 -> 7.1 ms, q3_K)
    if (derived_sums(out, st, s, r, rs, d, act_sub, sub > act_sub ? sub : 0, keep, true, dot)) acc = d.with;
    if (d.unsigned_codes.defined()) {
        wc = {d.unsigned_codes};
        keep.push_back(d.unsigned_codes);
    }
    if (sub <= per) sub = 0;
    sub ? s.split(r.x, rxs, rxc, sub).split(rxc, rxc, rxi, per) : s.split(r.x, rxc, rxi, per);
    Func blk = s.rfactor(ry, u);
    Func rest, mid, codes = block_dot(blk, rs, true, rest, mid, false, keep);
    // per block, per 16 columns (mc): the rows unrolled. A macro tile (more
    // than 8 rows), as GGML's 256 x 256 windows: per block, its 8-row strips
    // (nb) across all its columns, each block's weights decoded once for them.
    Var mo("mc"), mi("mv"), nb("nb");
    const bool macro = ts.nt > 8;
    // (the macro's own loops under cond: s, acc and the sums are hoisted out
    // of dot, so their stages are every branch's)
    s.split(m, mo, mi, lanes);
    if (macro) {
        s.specialize(cond).split(n, nb, n, 8).reorder(mi, n, nb, mo, ry).vectorize(mi).unroll(n);
        s.split(n, nb, n, 1).reorder(mi, n, nb, mo, ry).vectorize(mi).unroll(n);
    } else {
        s.reorder(mi, n, mo, ry).vectorize(mi).unroll(n);
    }
    std::vector<Func> accs{dot};
    if (acc.name() != dot.name()) accs.push_back(acc);
    // (dot's pure stage is every tile's: scheduled once, by the first call;
    // in 8-row strips only in a macro tile's own acc)
    for (Func f : accs) {
        bool shared = f.name() == dot.name();
        if (shared && !first) continue;
        f.reorder_storage(m, n);
        if (macro && !shared) {
            Var v16("mv16");
            f.specialize(cond).split(m, m, v16, lanes).split(n, nb, n, 8).reorder(v16, n, m, nb).vectorize(v16).unroll(n);
        }
        f.vectorize(m, lanes).unroll(n);
    }
    // (the derived sums' difference added to dot: across the columns, as
    // dot is stored, not a scalar loop striding the rows: q4_K 9% of M = 512)
    // (under cond: an AMX tile's accumulator elsewhere is read whole)
    if (accs.size() > 1) dot.update(0).specialize(cond).reorder(m, n).vectorize(m, lanes, TailStrategy::GuardWithIf);
    LoopLevel cols(acc, macro ? VarOrRVar(nb) : VarOrRVar(mo)), blocks(acc, ry);
    std::vector<Func> readers{blk, codes};
    for (Func f : {blk, codes}) {
        f.compute_at(cols).reorder_storage(m, n).vectorize_all_stages(m).unroll_all_stages(n, u);
    }
    Func inner = codes;
    if (sub) {
        Func part = codes.update().rfactor(rxs, v);
        inner = part.update().hoist_invariants()[0];
        codes.update().reorder(m, n, rxs).vectorize(m).unroll(n);
        part.compute_at(codes, rxs).reorder_storage(m, n).vectorize_all_stages(m).unroll_all_stages(n, u, v);
        inner.compute_at(codes, rxs).reorder_storage(m, n).vectorize_all_stages(m).unroll_all_stages(n, u, v);
        readers.insert(readers.end(), {part, inner});
        for (Func f : sc) {
            if (Func g = f.clone_in_callers({part}); g.defined()) {
                g.compute_at(blocks).vectorize(g.args()[0], block / sub);
                if (!macro) {
                    for (size_t i = 1; i < g.args().size(); i++)
                        g.unroll(g.args()[i]);
                }
            }
        }
    }
    inner.update().reorder(rxi, m, n, rxc).atomic().vectorize(rxi).vectorize(m).unroll(n).unroll(rxc);
    // the weight's codes decoded per block, the act's staged up front
    std::vector<Func> wd = rs[0].decode_funcs();
    bool lut = std::any_of(wd.begin(), wd.end(), [&](const Func &f) { return tabulate(rs[0], f, true); });
    for (Func f : wc) {
        if (!calls(inner, f)) continue;
        Func w = f.clone_in(inner);  // (a clone: each tile's tables clone in its own)
        readers.push_back(w);
        // (unrolled, the sub-blocks' bit field positions constant, not a
        // shift per lane widened to 32 bits: q6_K 7.1 -> 4.5 ms, q2_K 5.0 ->
        // 4.3, q5_K 4.8 -> 4.1; not a table's index: iq3_s 3% slower)
        Var wk = w.args()[0], wko("wko"), wki("wki");
        w.compute_at(blocks).split(wk, wko, wki, t.natural_vector_size<int8_t>()).vectorize(wki);
        if (!macro) w.unroll(w.args()[2]);
        if (!lut) w.unroll(wko);
    }
    for (Func f : ac) {
        if (!calls(inner, f)) continue;
        Func a = f.in(inner);
        Var j = a.args()[0], b = a.args()[1], am = a.args()[2], jo("jo"), ji("ji"), mo2("mo2"), mi2("mi2");
        a.compute_root().split_storage(j, jo, ji, per).reorder_storage(ji, am, jo, b);
        a.split(j, jo, ji, per).split(am, mo2, mi2, lanes, TailStrategy::GuardWithIf).reorder(ji, mi2, jo, mo2, b).vectorize(ji).vectorize(mi2).parallel(b);
    }
    if (d.sums.defined()) column_sums(d, out, st, n, m, r, lanes, rs, macro ? cond : Expr());
    tables(rs, readers, blocks, t);
}

// The Funcs of e's encoder: those of the result that encodes e.
inline std::vector<Func> encoder_funcs(Func e, const std::vector<ApproximationResult> &rs) {
    for (const ApproximationResult &res : rs) {
        if (res.encoded[0].name() == e.name()) return res.encode_funcs();
    }
    return {};
}

// An encoder computed per record, vectorized: its per-value Funcs across
// their elements, its order-independent reductions across the block (a
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
            if (f.update().is_order_independent()) f.update().atomic(true).vectorize(f.rvars()[0]);  // (just checked)
        }
    }
    if (vec) e.vectorize(e.args()[0], vec, TailStrategy::GuardWithIf);
}

// The N = M = 1 schedule (GGML's vec_dot). Staged activations are encoded
// once, up front.
inline void vec_dot(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, const Target &t, const Approximation &acc = Approximation(), const Knobs &k = Knobs()) {
    luts(rs, true, t);
    for (Func e : staged) {
        encoder(e.compute_root(), rs, fmt.act_block == 1 ? 4 * t.natural_vector_size<float>() : 0);
    }
    out.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);  // N = M = 1
    if (fmt.block == 1) {
        // 8 interleaved steps: f16 vec_dot 1.2x, gemv 1.05-1.1x of 4 (M3)
        acc.defined() ? flat_tree(out, 0, dots[0], Expr(), {1, 1, 4}, r, acc, staged) : flat(out, 0, dots[0], Expr(), {1, 1, 8}, r, t, bf16_pairs(rs, t));
        return;
    }
    int wper = t.arch == Target::X86 && !fmt.sub && fmt.act_block == fmt.block ? fmt.per : 1;
    blocked(out, 0, dots[0], Expr(), {1, 1, k.interleave}, r, fmt.block, fmt.act_block == fmt.block, rs, t, true, fmt.sub, fmt.act_sub, k, true, wper);
}

// The Metal tiles of out: threadgroups of `sg` simdgroups, each `rows(cols)`
// rows of `cols` columns, the widest cols <= 8 that M allows (gemv: nr rows
// of one column); N < nr * sg, a row per threadgroup. Edge tiles shift in.
// `sg8`: the simdgroups of the 8-column tiles' threadgroups, if not sg.
inline void metal_tiles(Func out, Func dot, int nr, int sg, const std::function<int(int)> &rows, int sg8 = 0) {
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
        tile(big.specialize(M >= cols), rows(cols), cols == 8 && sg8 ? sg8 : sg, cols);
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
// the gemv lanes there 0.5-1.4x), behind dot's specialization. `on_codes`:
// whether dot reads rs[0]'s codes (not a weight compute format's values,
// e.g. q4_K:f16's), from which the scales and offsets hoist. `integer`
// (integer acts): the codes' dot in int32, each lane's run of 8 values one
// vector reduction (CUDA: two dp4a), as is an affine weight's sum of the act's
// codes (its min's term; CUDA: dp4a by ones, reusing the codes' loads: q4_1,
// q5_1, q4_K, q5_K at M = 8 1.6-2.4x).
inline void metal_rows(Func out, Func dot, const RDom &r, int block, const std::vector<ApproximationResult> &rs, int sub = 0, bool on_codes = true,
                       bool integer = false, int sg = 2) {
    Var n = out.args()[0], m = out.args()[1], ns("ns"), ub("ub"), u("u"), l("l");
    RVar rx = r.x, ry = r.y, ryo("ryo"), ryi("ryi"), rl("rl");
    Expr M = out.output_buffer().dim(1).extent();
    if (!on_codes) sub = 0;
    int bits = 0;
    while ((1 << bits) < block)
        bits++;
    if (block != 1 << bits || bits < 5) throw std::invalid_argument("metal_rows: blocks of 2^k >= 32 values");
    const int run = 3, vals = std::min(5, bits - 1);
    const int nr = std::max(1, (8 >> (vals - 4)) / sg);
    // the bits each kind: run, shared, the lane's
    enum Kind { Run,
                Shared,
                Lane };
    std::vector<Kind> kind(bits, Lane);
    for (int b = 0; b < run; b++) {
        kind[b] = Run;
    }
    std::vector<int> count = shared_bits(dot, r, block, 1, run), order;
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
    // (integer: 4 rows at every M; at M = 8 2 rows 0.72-0.89x, 8 rows
    // 0.74-1.07x; RTX A4500)
    // (8 columns: 2 warps a block also where the gemv takes one; CUDA, q4_K
    // on f32 acts at 4096^2 x 64..512 1.05-1.08x)
    metal_tiles(out, dot, nr, sg, [=](int cols) { return cols > 4 && !integer ? 2 : 4; }, std::max(sg, 2));
    // (integer: the gemv's lanes at every M, their runs the dp4a's)
    if (!integer) {
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
    }
    Stage s = dot.update();
    // r.x in segments of one kind, from the lowest
    struct Seg {
        RVar r;
        Kind kind;
        int lo;  // (its lowest bit)
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
            segs.push_back({in, kind[b], b});
            rest_x = o;
        } else {
            segs.push_back({rest_x, kind[b], b});
        }
        b = e;
    }
    std::vector<std::pair<RVar, Var>> lane_vars, acc_vars, sub_vars;
    std::vector<RVar> lane_rs, inner_rs, sub_rs;
    // (integer, float sub-block scales: nvfp4's, whose nibbles share bytes
    // within a sub-block; its scale hoists out of the int32 dot where its
    // bits are the lane's, so a shared bit below them is of the dot)
    const bool fsub = integer && float_sub_scales(rs);
    // (runs first: innermost)
    std::stable_partition(segs.begin(), segs.end(), [](const Seg &g) { return g.kind == Run; });
    for (const Seg &g : segs) {
        if (g.kind == Lane) {
            lane_vars.emplace_back(g.r, Var("uh" + std::to_string(lane_vars.size())));
            acc_vars.emplace_back(g.r, Var("ua" + std::to_string(acc_vars.size())));
            lane_rs.push_back(g.r);
        } else if (sub && g.kind == Shared && (!fsub || (1 << g.lo) >= sub)) {
            sub_vars.emplace_back(g.r, Var("v" + std::to_string(sub_vars.size())));
            sub_rs.push_back(g.r);
        } else {
            inner_rs.push_back(g.r);
        }
    }
    lane_vars.emplace_back(ry, u);
    Func blk = s.rfactor(lane_vars), rest, mid;
    Func codes = block_dot(blk, rs, integer, rest, mid, on_codes && affine(rs));
    // the runs (int32 dots, and an affine weight's act code sums) reduced as
    // vectors, else unrolled
    auto inner_loops = [&](Stage fs) {
        for (const RVar &v : inner_rs) {
            if (integer && v.name() == inner_rs[0].name() && segs[0].kind == Run) {
                fs.atomic().vectorize(v);
            } else {
                fs.unroll(v);
            }
        }
    };
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
        inner_loops(fs);
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
        inner_loops(inner.update());
    }
    // (integer: every column of the tile, registers)
    if (integer) {
        std::vector<Func> fs{acc, blk, mid, codes, rest};
        for (auto [f, part, inner] : subs) {
            fs.insert(fs.end(), {part, inner});
        }
        for (Func f : fs) {
            if (!f.defined()) continue;
            for (int i = 0; i <= f.num_update_definitions(); i++) {
                Stage fs = i ? f.update(i - 1) : Stage(f);
                for (const Var &v : f.args()) {
                    if (v.same_as(n) || v.same_as(m)) fs.unroll(v);
                }
            }
        }
    }
}

// Metal on float weights, as GGML's kernel_mul_mv_f16_f32_4: metal_tiles of
// 2 rows (gemv) or 4 rows of up to 8 columns. A lane takes `c` contiguous
// values of every 32 * c (coalesced; merged into half4/float4 loads), each
// output a scalar accumulator (float4 ones spill for M > 1). Staged acts get
// a kernel of their own (inlined, every tile would encode its columns).
inline void metal_flat(Func out, Func dot, const RDom &r, const std::vector<Func> &staged, int sg = 2) {
    const int nr = 4 / sg, c = 16;
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
// storage's), else false. With `bf16` (CUDA's wmma has bf16 fragments), bf16
// operands alike.
inline bool f16_operands(const std::vector<ApproximationResult> &rs, const std::vector<Func> &staged, const Formats &fmt, Func &wh, Func &xh, bool bf16 = false) {
    const ApproximationResult *w = fmt.wcompute ? &rs.back() : fmt.block == 1 && !rs.empty() && !fmt.wplain ? &rs[0] :
                                                                                                              nullptr;
    size_t acts = rs.size() - fmt.wcompute;
    if (!w || w->encoded.size() != 1) return false;
    wh = w->encoded[0];
    xh = staged.size() == 1 ? staged[0] : acts == 2 && rs[1].encoded.size() == 1 ? rs[1].encoded[0] :
                                                                                   Func();
    return xh.defined() && wh.type() == xh.type() && (wh.type() == Float(16) || (bf16 && wh.type() == BFloat(16)));
}

// The f32 operands of a Metal gemm on exact weights (wh: the weights' decoded
// values; xh: x, the acts dot multiplies them by; flat: the act's decoded
// values, e.g. f32:bf16's), else false. (bf16 weights: Metal C has no bfloat
// fragments; on f32 ones the gemm takes 0.11-0.5x flat's rows' time at M =
// 32..512; M3 Max)
inline bool f32_operands(Func dot, Func x, const std::vector<ApproximationResult> &rs, const std::vector<Func> &staged, const Formats &fmt, Func &wh, Func &xh) {
    if (fmt.wcompute || fmt.wplain || rs.empty() || (fmt.block % 8 || !staged.empty()) && fmt.block != 1) return false;
    wh = rs[0].replacement;
    xh = fmt.block == 1 && rs.size() > 1 ? rs.back().replacement : x;
    return dot.type() == Float(32) && wh.type() == Float(32) && xh.type() == Float(32);
}

// A GPU gemm's tile shape under `cond` (Expr(): the rest): a block of wn x wm
// warps (Metal: simdgroups), each tn x tm fragments; `guard`: one tile, its
// stores guarded (gpu_staged()); `kstep`: the values of K staged per step;
// `pad_rows`: rows past a block's in its staged operands (gpu_int_gemm()).
struct GemmShape {
    Expr cond;
    int tn, tm, wn;
    bool guard = false;
    int wm = 1, kstep = 64, pad_rows = 2;
};

// How gpu_staged() stages a step's operands in shared memory: each lane
// stores `vec` values of K at once; `pad`: a row's storage is its step's
// values and `pad` more (CUDA: a row a multiple of 128 bytes after the last
// puts a fragment's rows in the same banks, and its loads conflict: 8 values
// more on f16 x f16 at 4096 x 4096 x 64, 128 x 64 tiles: 0.22 -> 0.18 ms; RTX
// 3090); wcopy, xcopy: the weights, the acts are their input as stored,
// copied by the copy engine (MemoryType::GPUSharedAsync: 0.18 -> 0.13 ms);
// xroot: the acts are encoded once per call, in a kernel of their own, and
// its output copied (each tile otherwise encodes its rows of them: f16 on
// f32:f16 acts at 4096 x 4096 x 16 0.066 -> 0.046 ms, x 512 0.47 -> 0.31);
// xasync: the acts' copies are the copy engine's too (beside weights decoded
// in the kernel, plain loads: q4_0:f16 on f16 acts at 4096 x 4096 x 32 0.073
// -> 0.058 ms, on f32:f16 acts 0.074 -> 0.064); sub16: super-blocks' weights
// on the one-fragment tiles 16 values a lane, a sub-block's scale decoded
// once per 16 (q4_K:f16 at M = 16 0.066 -> 0.060 ms).
struct Staging {
    int vec = 4, pad = 0;
    bool wcopy = false, xcopy = false, xroot = false, xasync = true, sub16 = false;
};

// Metal's shapes, of 8 x 8 fragments, by M: 64 x 64 on blocked f16 operands
// (or 64 x 56) on grids of >= 512 of them, else 64 x 32 (M >= 256, N >= 64),
// 32 x 32, 16 x 16, 16 x 8. On f16 operands at 16 < M < 32 one 32 x 32
// tile, guarded (decoding the weights once, not in two shifted tiles).
inline std::vector<GemmShape> metal_shapes(Func out, Func wh, const Formats &fmt) {
    Expr M = out.output_buffer().dim(1).extent(), N = out.output_buffer().dim(0).extent();
    const bool h = wh.type() == Float(16), hb = h && fmt.block > 1;
    std::vector<GemmShape> shapes;
    if (h) shapes.push_back({M > 16 && M < 32, 1, 4, 4, true});
    if (hb) {
        // (64 x 56 where it computes fewer rows past M than 64 x 64; fewer
        // than 512 tiles of 64 x 64 load the GPU's cores unevenly: 4096 x
        // 4096 x 256 0.92-0.94 -> 0.95-0.96x of GGML's on 64 x 32, but 4096
        // x 4096 x 512 1.01 -> 0.95x; M3 Max)
        Expr big = M >= 256 && N >= 64 && cast<int64_t>(M) * N >= (1 << 21);
        shapes.push_back({big && (M + 55) / 56 * 56 < (M + 63) / 64 * 64, 2, 7, 4});
        shapes.push_back({big, 2, 8, 4});
    }
    shapes.insert(shapes.end(), {{M >= 256 && N >= 64, 2, 4, 4}, {M >= 32, 1, 4, 4}, {M >= 16, 1, 2, 2}, {Expr(), 1, 1, 2}});
    return shapes;
}

// CUDA's shapes, of wmma's 16 x 16 fragments, by M. Weights staged by the
// copy engine (`copied`; f16 x f16 at 4096 x 4096), each warp one fragment of
// N: 4 warps of 16 x 64 from M = 128 (M = 128 0.097 ms, 512 0.31; 8 warps
// of 16 x 32 0.097, 0.34; 4 of 32 x 64 0.13, 0.34), of 16 x 32 from 64 on
// 128-value steps (0.060; 64-value 0.073), below 2 warps of 16 x 16 (M = 16
// 0.045 on 256-value steps, 0.052 on 128; M = 32 0.048 on 128). A block's
// time is its steps' (a step's loads take ~1-2 us to arrive, whatever their
// size), so few rows of M take long steps; its shared memory (48 KB) bounds
// them. Below 16 rows one 16-row tile, guarded (M = 2..15 0.046-0.047 ms; the
// rows 0.048 at M = 2, 0.065 at 4, 0.111 at 8); from 17 to 31 one of 32 rows
// (0.057; two of 16, the second shifted, 0.065). Flat weights converted in
// the kernel (f32:f16): one warp per block of as many rows of M as it has
// accumulators for, 2 warps from M = 256 (f32:f16 on f32:f16 acts, M = 32:
// 0.087 ms; 128: 0.164; 512: 0.42), to M = 16 on 256-value steps (0.088; on
// 128-value steps 0.103), below 16 rows on a guarded tile (M = 15: 0.094; the
// rows 0.25). Blocked weights (`block` > 1), decoded in the kernel: 64 x 128
// of 2 x 2 warps from M = 256, each weight decoded once per 128 act rows; 128
// x 64 of 4 x 1 above 128; 32 x 64 of 2 x 2 from 64; 16 x 32 of 1 x 2 from
// 32; below one warp's 16 x 16 on 128-value steps. (ms at M = 16, 32, 64,
// 128, 512 on f32:f16 acts: q4_0:f16 0.056, 0.064, 0.098, 0.117, 0.40; one
// warp per block of 16 x 16 to 16 x 64, two of 16 x 128 from 256, 0.061,
// 0.069, 0.098, 0.130, 0.42; q4_K:f16 0.054, 0.058, 0.083, 0.112, 0.40
// against 0.067, 0.073, 0.093, 0.152, 0.45; q6_K:f16 0.078, 0.064, 0.083,
// 0.131, 0.44 against 0.082, 0.091, 0.114, 0.158, 0.62.) `one_warp`: one warp
// of 16 x 16 to M = 63 and of 16 x 32 to 127 (q8_0:f16 M = 32 0.084 -> 0.067,
// 64 0.117 -> 0.091; q5_0:f16 alike). (RTX 3090)
inline std::vector<GemmShape> cuda_shapes(Func out, bool copied, int block, bool one_warp = false) {
    Expr M = out.output_buffer().dim(1).extent(), N = out.output_buffer().dim(0).extent();
    auto shape = [](Expr cond, int tn, int tm, int wn, bool guard = false, int kstep = 64, int wm = 1) {
        GemmShape sh{cond, tn, tm, wn, guard, wm};
        sh.kstep = kstep;
        return sh;
    };
    if (copied) {
        return {shape(M >= 128 && N >= 64, 1, 4, 4), shape(M >= 64 && N >= 64, 1, 2, 4, false, 128), shape(M >= 32, 1, 1, 2, false, 128),
                shape(M > 16, 1, 2, 2, true, 128), shape(M >= 16, 1, 1, 2, false, 256), shape(Expr(), 1, 1, 2, true, 256)};
    }
    if (block > 1) {
        std::vector<GemmShape> shapes{shape(M >= 256 && N >= 64, 2, 4, 2, false, 64, 2), shape(M > 128 && N >= 128, 2, 4, 4)};
        if (one_warp) {
            shapes.insert(shapes.end(), {shape(M >= 128, 1, 2, 2, false, 64, 2), shape(M >= 64, 1, 2, 1)});
        } else {
            shapes.insert(shapes.end(), {shape(M >= 64, 1, 2, 2, false, 64, 2), shape(M >= 32, 1, 1, 1, false, 64, 2)});
        }
        shapes.push_back(shape(Expr(), 1, 1, 1, false, 128));
        return shapes;
    }
    return {shape(M >= 256, 1, 8, 2), shape(M >= 128, 1, 4, 1), shape(M >= 64, 1, 2, 1), shape(M > 16, 1, 1, 1, false, 128),
            shape(M >= 16, 1, 1, 1, false, 256), shape(Expr(), 1, 1, 2, true, 256)};
}

// What gpu_staged() leaves to fragments(): the operands' copies in shared
// memory (ws, xs), per step of K (ks), and their fragments (wf, xf), per
// fragment of K (ko).
struct Staged {
    Func ws, xs, wf, xf;
    RVar ks, ko;
};

// A GPU gemm on f16 or f32 operands (wh, xh), as GGML's Metal kernel_mul_mm,
// for `o` (out's specialization): the macro-kernel. A block (Metal: a
// threadgroup) takes a bn x bm tile, its warps wn x wm sub-tiles of frag * tn
// x frag * tm, each frag x frag an accumulator (fragments()). Per step of K
// (the shape's kstep values, or the largest of its halves down to 32, or a
// block, that K is a multiple of) the operands are staged in shared memory
// (`sg`). Tiles: the first of `shapes` whose cond holds. Under a guarded
// shape, out reads the tile from a shared copy (edge; a guarded store from
// Tile memory doesn't lower), its stores guarded; the rows past M it computes
// read the last act row (the algorithm clamps them).
inline Staged gpu_staged(Stage o, Func out, Func dot, Func edge, Func wh, Func xh, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs,
                         const std::vector<GemmShape> &shapes, int frag, const Staging &sg = Staging()) {
    if (fmt.block % frag && fmt.block != 1) throw std::invalid_argument("gpu_staged: flat, or blocks of a multiple of frag values");
    // (names apart from the other specializations' loops, which compute
    // other Funcs at theirs)
    Var n = out.args()[0], m = out.args()[1], ni("mm_ni"), mi("mm_mi"), nw("mm_nw"), mw("mm_mw"), nt("mm_nt"), mt("mm_mt");
    RVar rx = r.x, ry = r.y, ks("ks"), kb("kb"), ko("ko"), kt("kt"), rxk("rxk");
    Expr nb = r.y.extent();
    // dot reads wh and xh directly, through shared copies (ws, xs) and their
    // fragments (wf, xf)
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
    // Copied acts (sg.xcopy) under a guarded shape: the rows past M are no
    // copy of the input's, so its tile copies a copy of them made by a
    // kernel of its own (xg, realized under that shape only); the other
    // shapes inline it.
    const auto guarded = std::count_if(shapes.begin(), shapes.end(), [](const GemmShape &sh) { return sh.guard; });
    Func xg;
    if (sg.xcopy && guarded) xg = xh.in(xs);
    Stage sd = Stage(dot), su = dot.update();
    std::vector<std::function<void()>> edged;
    // the steps of K by their conditions, first first
    std::vector<std::pair<Expr, int>> rows;
    for (const GemmShape &sh : shapes) {
        const int kstep = sh.kstep;
        const int bn = frag * sh.tn * sh.wn, bm = frag * sh.tm * sh.wm;
        auto tile = [&](Stage st) {
            if (sh.guard) st.tile({n, m}, {nw, mw}, {ni, mi}, {frag * sh.tn, frag * sh.tm}, {TailStrategy::Auto, TailStrategy::RoundUp});
            else
                st.tile(n, m, nw, mw, ni, mi, frag * sh.tn, frag * sh.tm);
            return st.tile(ni, mi, nt, mt, frag, frag).gpu_threads(nw, mw).unroll(ni).unroll(mi).vectorize(nt).vectorize(mt);
        };
        Stage so = o.specialize(sh.cond.defined() ? sh.cond : Internal::const_true());
        if (sh.guard) {
            Var sw("mm_sw");
            so.split(n, n, ni, bn, TailStrategy::ShiftInwards).split(m, m, mi, bm / sh.wn, TailStrategy::GuardWithIf).split(m, m, sw, sh.wn, TailStrategy::RoundUp).reorder(ni, mi, sw, n, m);
            so.gpu_blocks(n, m).gpu_lanes(ni).gpu_threads(sw);
            // (edge's tile under the shape's cond, where several guard)
            edged.push_back([=]() mutable {
                edge.compute_at(out, n).store_in(MemoryType::GPUShared);
                tile(guarded > 1 && sh.cond.defined() ? Stage(edge).specialize(sh.cond) : Stage(edge));
            });
        } else {
            so.eager_inline(edge);
            so.tile(n, m, ni, mi, bn, bm, TailStrategy::ShiftInwards).tile(ni, mi, nw, mw, ni, mi, frag * sh.tn, frag * sh.tm).tile(ni, mi, nt, mt, frag, frag);
            so.gpu_blocks(n, m).gpu_threads(nw, mw).unroll(ni).unroll(mi).vectorize(nt).vectorize(mt);
        }
        tile(sd.specialize(sh.cond));
        Stage u = su.specialize(sh.cond);
        // ks: the steps of K; kb: their blocks (or, for blocks longer than a
        // step or flat, ko: the step's fragment-long pieces)
        auto steps = [&](Stage st, int step, Expr whole = Expr()) {
            Expr c = sh.cond.defined() ? sh.cond : Internal::const_true();
            rows.emplace_back(whole.defined() ? c && whole : c, step);
            if (whole.defined()) st = st.specialize(whole);
            tile(st);
            if (fmt.block == 1) {
                st.split(ry, ks, rxk, step).split(rxk, ko, kt, frag).reorder(kt, nt, mt, ni, mi, ko, rx, nw, mw, ks);
            } else if (fmt.block >= step) {
                st.split(rx, ks, rxk, step).split(rxk, ko, kt, frag).reorder(kt, nt, mt, ni, mi, ko, nw, mw, ks, ry);
            } else {
                st.split(ry, ks, kb, step / fmt.block).split(rx, ko, kt, frag).reorder(kt, nt, mt, ni, mi, ko, kb, nw, mw, ks).unroll(kb);
            }
            st.unroll(ko).atomic().vectorize(kt);
        };
        for (int st = kstep; st > std::max(fmt.block, 32); st /= 2) {
            steps(u, st, nb % (st / fmt.block) == 0);
        }
        steps(u, fmt.block == 1 ? 32 : std::min(fmt.block, kstep));  // (gpu_gemm: flat K % 32 == 0)
        // each lane sg.vec values of K of a row of the step's weights or acts
        // (blocked (j, b, i) or flat (k, i)), unrolled (f32 acts on 64 x 56
        // 0.92 -> 1.0x of GGML's); two-level weights on the M >= 256 tiles
        // 16, as GGML's dequantize_q4_K etc., decoding a sub-block's scale
        // once per 16 values (q4_K/q5_K/q3_K 1.15-1.3x, q6_K 1.1x; on the
        // smaller tiles 0.88-1.0x)
        for (Func f : {ws, xs}) {
            std::vector<Var> a = f.args();
            Var l("l"), ln("ln"), lt("lt"), sw("sw"), jv("jv");
            Stage s = f.specialize(sh.cond);
            if (xg.defined() && !sh.guard && f.name() == xs.name()) s.eager_inline(xg);
            if (f.name() == ws.name() && fmt.sub && sh.tn > 1) {
                Var ju("ju"), jh("jh");
                s.split(a[0], l, jv, 32).split(jv, jh, jv, 16).split(jv, ju, jv, 4).fuse(jh, l, l).unroll(ju);
            } else {
                // (super-blocks' weights on the one-fragment tiles: 16 values)
                s.split(a[0], l, jv, sg.sub16 && f.name() == ws.name() && fmt.block >= 256 && sh.tn == 1 ? 16 : sg.vec);
            }
            for (size_t i = 1; i < a.size(); i++) {
                Var fl("l" + std::to_string(i));
                s.fuse(l, a[i], fl);
                l = fl;
            }
            // the warps (sw, sm) on dot's thread dimensions (nw, mw)
            Var sm("sm");
            s.split(l, l, ln, 32).split(l, lt, sw, sh.wn * sh.wm).split(sw, sm, sw, sh.wn).reorder(jv, lt, ln, sw, sm);
            s.gpu_lanes(ln).gpu_threads(sw, sm).vectorize(jv);
            s.unroll(lt);
        }
    }
    for (auto &e : edged) {
        e();
    }
    if (xg.defined()) {
        Var k = xh.args()[0], ko("ko"), kv("kv");
        xg.compute_at(out, Var::outermost()).split(k, ko, k, 256, TailStrategy::GuardWithIf).split(k, k, kv, sg.vec).vectorize(kv).gpu_blocks(ko, xh.args()[1]).gpu_threads(k);
    }
    dot.compute_at(out, n);
    for (Func f : {ws, xs}) {
        const bool copy = f.name() == ws.name() ? sg.wcopy : (sg.xcopy || sg.xroot) && sg.xasync;
        f.compute_at(dot, ks).store_in(copy ? MemoryType::GPUSharedAsync : MemoryType::GPUShared);
        if (!sg.pad) continue;
        // a row's storage along K: the values of its step (blocked: of one
        // of its blocks) and sg.pad more
        Expr row = 0;
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
            row = select(it->first, (f.args().size() == 2 ? it->second : std::min(it->second, fmt.block)) + sg.pad, row);
        }
        f.align_storage(f.args()[0], row);
    }
    return {ws, xs, wf, xf, ks, ko};
}

// gpu_staged()'s micro-kernel: dot's frag x frag accumulators in Tile memory,
// the matrix unit's (Metal's simdgroup_matrix: 8; CUDA's wmma: 16), and the
// operand fragments of each frag values of K: xf's frag x frag * tm, wf's frag
// * tn x frag (its storage [row, k]: a fragment's rows are the weight's).
inline void fragments(const Staged &st, Func dot, int frag) {
    dot.store_in(MemoryType::Tile);
    for (Func f : {st.xf, st.wf}) {
        std::vector<Var> a = f.args();
        Var j = a[0], i = a.back(), jo("jo"), jt("jt"), io("io"), it("it");
        if (f.name() == st.wf.name()) {
            if (a.size() == 3) f.reorder_storage(i, j, a[1]);
            else
                f.reorder_storage(i, j);
        }
        f.compute_at(dot, st.ko).store_in(MemoryType::Tile).split(j, jo, jt, frag).split(i, io, it, frag);
        if (a.size() == 3) {
            f.reorder(jt, it, jo, io, a[1]).unroll(a[1]);
        } else {
            f.reorder(jt, it, jo, io);
        }
        f.unroll(jo).unroll(io).vectorize(jt).vectorize(it);
    }
}

// GPU on f16 or f32 operands: gpu_staged() on `shapes` of frag x frag
// fragments() where M >= min_m and N >= 32 (flat:
// K % 32 == 0 and `large`, else M * N >= 2^16: below it flat's rows win on
// Metal, 1.2-2.6x at M = 8..32, N = 512..2048), else metal_rows (flat: metal_flat) on a clone of
// dot, one kernel but flat's staged acts. min_m on Metal: 8 (GGML's; on f32
// at M = 8..15 the gemm takes 0.59-1.0x of the rows' time, iq4_nl's at M =
// 8 1.04x; M3 Max). n_align: the gemm only where N is a multiple of it. A
// tile's accumulators are stored straight to out, whose rows are N apart, the
// tiles at multiples of frag or shifted in from N: CUDA's wmma.store wants
// each fragment's rows 32 bytes aligned (an RTX 3090 faults on rows not 8
// bytes aligned: CUDA_ERROR_MISALIGNED_ADDRESS), i.e. N % 8 == 0. `sg`: the
// rows' warps per block (metal_rows(), metal_flat()); `staging`: gpu_staged()'s.
inline void gpu_gemm(Func out, Func dot, Func wh, Func xh, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs, int min_m,
                     const std::vector<GemmShape> &shapes, int frag, const std::vector<Func> &staged = {}, int n_align = 1, int sg = 2, const Staging &staging = Staging(),
                     Expr large = Expr()) {
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent();
    // out reads dot through a wrapper (via): the gemm's branch inlines it at
    // once, reading edge (dot's copy, inlined unless guarded); then via reads
    // pass, reading the clone, and the other branches inline both
    Func via = dot.in(out), edge = dot.in(via);
    Expr cond = M >= min_m && N >= 32;
    if (fmt.block == 1) cond = cond && r.y.extent() % 32 == 0 && (large.defined() ? large : cast<int64_t>(M) * N >= (1 << 16));
    if (n_align > 1) cond = cond && N % n_align == 0;
    Stage mm = Stage(out).specialize(cond).eager_inline(via);
    Func pass = edge.clone_in(via);
    Func rows = dot.clone_in(pass);
    out.eager_inline(via, pass);
    fragments(gpu_staged(mm, out, dot, edge, wh, xh, r, fmt, rs, shapes, frag, staging), dot, frag);
    // the rows' luts, staged before metal_rows inlines the decoders (else
    // its gemv inlines them: select trees)
    metal_luts(out, rs);
    // the gemm's acts encoded in a kernel of their own (staging.xroot; as
    // metal_flat()'s, scheduled after the rows' clones of them), realized in
    // out's gemm branches (at the root, the rows' calls would set up its
    // buffer too)
    auto root_acts = [&]() {
        if (!staging.xroot) return;
        Var k = xh.args()[0], em = xh.args()[1], ko("ko"), kv("kv");
        xh.compute_at(out, Var::outermost()).split(k, ko, k, 256, TailStrategy::GuardWithIf).split(k, k, kv, 4).vectorize(kv).gpu_blocks(ko, em).gpu_threads(k);
    };
    if (fmt.block > 1) {
        metal_rows(out, rows, r, fmt.block, rs, fmt.sub, !fmt.wcompute, false, sg);
        root_acts();
        return;
    }
    // the rows' own staged acts (their kernel skipped under the gemm, whose
    // tiles stage theirs)
    std::set<std::string> names;
    for (const Func &e : staged) {
        names.insert(e.name());
    }
    metal_flat(out, rows, r, clone_paths(rows, [&](const Func &f) { return names.count(f.name()) > 0; }), sg);
    root_acts();
}

// CUDA's shapes of the integer gemm (gpu_int_gemm()), by M, of mma.sync's
// 16 x 8 tiles (tn of 16 rows of N, tm of 8 of M, a warp's), on wn x wm warps.
// Warps of 32 x 64 (2 x 8 tiles; 16 x 128, 1 x 16, load an operand or scale
// tile for every two multiplies, 32 x 64 for every one and a half: q4_0 on
// f32:q8_0.soa at 4096 x 4096 x 512, 64 x 128 blocks: 0.261 -> 0.232 ms):
// 64 x 128 on 2 x 2 from M = 256 (128 x 64 of 4 x 1 warps 0.248, 64 x 128 of
// 32 x 128 0.258, 64 x 64 ones 0.254; on 64-value steps 0.229, which the
// weight blocks of several blocks do not take), 64 x 64 on 2
// x 1 from 64, 32 x 32 or 32 x 16, one warp, below (where K is not split; the
// K split's shapes below). (RTX 3090)
inline GemmShape ishape(Expr cond, int tn, int tm, int wn, bool guard, int kstep, int wm = 1, int pad_rows = 2) {
    GemmShape sh{cond, tn, tm, wn, guard, wm};
    sh.kstep = kstep;
    sh.pad_rows = pad_rows;
    return sh;
}

inline std::vector<GemmShape> cuda_int_shapes(Func out, bool wmma) {
    Expr M = out.output_buffer().dim(1).extent(), N = out.output_buffer().dim(0).extent();
    if (wmma) {
        return {ishape(M >= 256 && N >= 64, 1, 8, 4, false, 128), ishape(M >= 64 && N >= 64, 1, 4, 4, false, 128), ishape(M >= 32, 1, 2, 1, false, 256), ishape(Expr(), 1, 1, 1, false, 256)};
    }
    return {ishape(M >= 256 && N >= 64, 2, 8, 2, false, 128, 2), ishape(M >= 64 && N >= 64, 2, 8, 2, false, 128), ishape(M >= 32, 1, 4, 1, false, 256),
            ishape(Expr(), 1, 2, 1, false, 256)};
}

// A split of K in kz (gpu_int_gemm()), where cond holds, on its shapes;
// `wstep`: a weight block of several blocks staged per step, not whole.
struct KSplit {
    int kz;
    Expr cond;
    std::vector<GemmShape> shapes;
    bool wmma = false, wstep = false;
};

// The K splits (gpu_int_gemm()'s KSplit), by M, each block's tile the
// first of its shapes at or above M's rows (rounded up; shifted tiles of
// fewer rows decode a weight tile twice: q4_0 at 4096 x 4096 x 24 / 48 /
// 96 0.037 / 0.050 / 0.075 -> 0.029 / 0.042 / 0.052 ms): in 2 from M = 33
// to 255, 64 x 128 blocks of 2 x 2 warps above 96 (q4_0 at 4096 x 4096 x
// 128: 0.070 ms; in 4 0.078; unsplit 64 x 64 0.076), 64 x 96 above 64, 64
// x 64 of 2 x 1 above 48 (M = 64: 0.046; in 4, 64 x 64 of 2 x 2 0.049), 64
// x 48 below (gpu_int_gemm() keeps it for some formats), 32 x 32 where N <
// 64; in 4 to 32, one warp of 32 x 32 above 16 (0.032; 2 warps 0.034, 4
// 0.036), else of 32 x 16 (M = 16: 0.027; 16 x 16 0.029, 64 x 16 of 2 warps
// 0.029; in 2 or 8 0.035, 0.031), on 128-value steps. On wmma's fragments
// (wmma_tiny to M = 16, wmma_small to 32, wmma_mid to 64 or, with
// wmma_big, below 128, wmma_big below 256), Phase 5's: in 4 below 128, 16 x
// 32 of 2 warps above 16, 16 x 16 below, 32 x 64 of 2 above 32; unsplit
// above. (RTX 3090)
inline std::vector<KSplit> cuda_int_splits(Func out, bool wmma_tiny, bool wmma_small, bool wmma_mid, bool wmma_big) {
    Expr M = out.output_buffer().dim(1).extent(), N = out.output_buffer().dim(0).extent();
    std::vector<KSplit> splits;
    // (to 16 and above: each shape where it holds; one split of both where
    // they are on the same tiles)
    auto small = [&](bool wm, Expr cond, bool tiny, bool rest) {
        std::vector<GemmShape> shapes;
        if (wm) {
            if (rest) shapes.push_back(ishape(tiny ? M > 16 : Expr(), 1, 2, 2, false, 128));
            if (tiny) shapes.push_back(ishape(Expr(), 1, 1, 1, false, 128));
            splits.push_back({4, cond, shapes, true});
        } else {
            // (32 x 32 unpadded: padded, 7 blocks of it no longer fit an SM's
            // shared memory, 512 of them at N = 4096 take two waves: M = 32
            // 0.030 -> 0.033 ms; so a weight block of several blocks staged
            // per step: q4_K's whole, 5 blocks an SM, 34.9 -> 27.7 us)
            if (rest) shapes.push_back(ishape(tiny ? M > 16 : Expr(), 2, 4, 1, false, 128, 1, 0));
            if (tiny) shapes.push_back(ishape(Expr(), 2, 2, 1, false, 128));
            splits.push_back({4, cond, shapes, false, true});
        }
    };
    if (wmma_tiny == wmma_small) {
        small(wmma_small, M <= 32, true, true);
    } else {
        small(wmma_tiny, M <= 16, true, false);
        small(wmma_small, M <= 32, false, true);
    }
    if (wmma_mid && wmma_big) {
        // (unsplit from M = 128: q1_0 at 4096 x 4096 x 128 0.078 ms; in 2,
        // 64 x 64 of 4 warps, 0.080)
        splits.push_back({4, M < 128, {ishape(Expr(), 1, 4, 2, false, 128)}, true});
    } else if (!wmma_mid && !wmma_big) {
        splits.push_back({2, M < 256, {ishape(M > 96 && N >= 64, 2, 8, 2, false, 128, 2), ishape(M > 64 && N >= 64, 2, 6, 2, false, 128, 2), ishape(M <= 48 && N >= 64, 2, 6, 2, false, 128), ishape(N >= 64, 2, 8, 2, false, 128), ishape(Expr(), 2, 4, 1, false, 128)}});
    } else if (wmma_mid) {
        splits.push_back({4, M <= 64, {ishape(Expr(), 1, 4, 2, false, 128)}, true});
        splits.push_back({2, M < 256, {ishape(N >= 64, 2, 8, 2, false, 128, 2), ishape(Expr(), 2, 4, 1, false, 128)}});
    } else {
        // (wmma's from M = 128: unsplit, the gemm on all of K's)
        splits.push_back({2, M < 128, {ishape(M > 64 && N >= 64, 2, 6, 2, false, 128, 2), ishape(M <= 48 && N >= 64, 2, 6, 2, false, 128), ishape(N >= 64, 2, 8, 2, false, 128), ishape(Expr(), 2, 4, 1, false, 128)}});
    }
    return splits;
}

// f's increment's calls of `fs` take the arguments its other calls promise
// clamped (promise_clamped, as rfactor leaves an RVar's bounds on the Var it
// becomes), where they are the same value: so each reads within those bounds.
inline void promised_args(Func f, const std::vector<Func> &fs) {
    using namespace Internal;
    Expr &v = f.function().update(0).values()[0];
    v = substitute_in_all_lets(v);
    auto bare = [](const Expr &e) {
        return simplify(mutate_with(e, [](auto *self, const Call *c) -> Expr {
            return c->is_intrinsic(Call::promise_clamped) || c->is_intrinsic(Call::likely) ? self->mutate(c->args[0]) : self->visit_base(c);
        }));
    };
    std::vector<Expr> promised;
    visit_with(v, [&](auto *self, const Call *c) {
        if (c->is_intrinsic(Call::promise_clamped)) promised.push_back(c);
        self->visit_base(c);
    });
    v = mutate_with(v, [&](auto *self, const Call *c) -> Expr {
        if (c->call_type != Call::Halide || !has(fs, c->name)) return self->visit_base(c);
        std::vector<Expr> args = c->args;
        for (Expr &a : args) {
            for (const Expr &p : promised) {
                if (a.type() == p.type() && can_prove(bare(p) == bare(a))) {
                    a = p;
                    break;
                }
            }
        }
        return Call::make(c->type, c->name, args, c->call_type, c->func, c->value_index, c->image, c->param);
    });
}

// f's increment's terms, each a product: its factors free of `cross` (and of
// f's own reductions: `tiles`, the Funcs it sums) become one Func of f's
// other pure vars it reads, `along` last, which the term then calls (a block's scale
// per weight row, or per act row: what a kernel stages per step). Returns
// them.
inline std::vector<Func> factor_funcs(Func f, const Var &along, const Var &cross, const std::vector<Func> &tiles, const std::string &name) {
    using namespace Internal;
    std::vector<Var> as;
    for (const Var &v : f.args()) {
        if (!v.same_as(along) && !v.same_as(cross)) as.push_back(v);
    }
    as.push_back(along);
    auto reads = [](const Expr &e, const std::string &var) {
        bool r = false;
        visit_with(e, [&](auto *, const Variable *x) { r |= x->name == var; });
        return r;
    };
    auto tile = [&](const Expr &e) {
        const Call *c = e.as<Call>();
        return c && (c->name == f.name() || has(tiles, c->name));
    };
    std::vector<Func> fs;
    std::function<Expr(const Expr &)> terms = [&](const Expr &e) -> Expr {
        if (const Add *a = e.as<Add>()) return terms(a->a) + terms(a->b);
        if (const Sub *a = e.as<Sub>()) return terms(a->a) - terms(a->b);
        if (tile(e)) return e;
        std::vector<Expr> factors{e};
        Expr mine, rest;
        for (size_t i = 0; i < factors.size(); i++) {
            const Expr x = factors[i];
            if (const Mul *m = x.as<Mul>()) {
                factors.push_back(m->a);
                factors.push_back(m->b);
            } else if (!tile(x) && !reads(x, cross.name()) && !is_const(x)) {
                mine = mine.defined() ? mine * x : x;
            } else {
                rest = rest.defined() ? rest * x : x;
            }
        }
        if (!mine.defined()) return e;
        std::vector<Var> gv;
        for (const Var &v : as) {
            if (v.same_as(along) || reads(mine, v.name())) gv.push_back(v);
        }
        std::vector<Expr> ga(gv.begin(), gv.end());
        Func g(unique_name(name));
        g(gv) = mine;
        fs.push_back(g);
        return rest.defined() ? g(ga) * rest : g(ga);
    };
    Expr &v = f.function().update(0).values()[0];
    v = terms(substitute_in_all_lets(v));
    return fs;
}

// f's increment over the first (e / l) * l values of its RVar rv, of
// extent e: a split of rv by a factor of l then divides it, by the
// simplifier alone. That is f's increment only where l divides e, so every
// definition of `out` or of a Func it calls that calls f must be under a
// specialization on e % l == 0 (checked here). Before any directive that
// copies or splits f's increment (split, specialize, rfactor).
inline void round_extent(Func f, const RVar &rv, int l, Func out) {
    using namespace Internal;
    const std::string err = "round_extent: " + f.name();
    if (f.num_update_definitions() != 1) throw std::invalid_argument(err + " has more than one update");
    Definition &d = f.function().update(0);
    if (!d.specializations().empty() || !d.schedule().splits().empty()) throw std::invalid_argument(err + "'s update is already specialized or split");
    std::vector<ReductionVariable> rvs = d.schedule().rvars();
    auto v = std::find_if(rvs.begin(), rvs.end(), [&](const ReductionVariable &x) { return x.var == rv.name(); });
    if (v == rvs.end()) throw std::invalid_argument(err + "'s update has no RVar " + rv.name());
    const Expr divides = simplify(v->extent % l == 0);
    std::function<void(const Definition &, bool)> reads = [&](const Definition &def, bool under) {
        bool calls = false;
        auto call = [&](auto *self, const Call *c) {
            calls |= c->call_type == Call::Halide && c->name == f.name();
            self->visit_base(c);
        };
        for (const Expr &e : def.args()) {
            visit_with(e, call);
        }
        for (const Expr &e : def.values()) {
            visit_with(e, call);
        }
        if (def.predicate().defined()) visit_with(def.predicate(), call);
        if (calls && !under) throw std::invalid_argument(err + " is read where " + rv.name() + "'s extent may not be a multiple of " + std::to_string(l));
        for (const Specialization &s : def.specializations()) {
            std::vector<Expr> cs;
            split_into_ands(s.condition, cs);
            bool w = under;
            for (const Expr &c : cs) {
                w |= equal(simplify(c), divides);
            }
            reads(s.definition, w);
        }
    };
    for (const auto &[name, g] : find_transitive_calls(out.function())) {
        if (name == f.name() || g.has_extern_definition()) continue;
        reads(g.definition(), false);
        for (const Definition &u : g.updates()) {
            reads(u, false);
        }
    }
    v->extent = (v->extent / l) * l;
    ReductionDomain dom(rvs, Expr(), true);
    LambdaMutator to_dom{[&](auto *self, const Variable *x) -> Expr {
        return x->reduction_domain.defined() ? Variable::make(x->type, x->name, dom) : Expr(x);
    }};
    d.mutate(&to_dom);
    dom.set_predicate(d.predicate());
    d.schedule().rvars() = rvs;
}

// CUDA on integer codes, as GGML's mul_mat_q: weights' codes on acts
// quantized in the pipeline to codes and a scale per block (a compute format
// of planar fields, e.g. f32:q8_0.soa), for out where M >= 16 (under a K
// split M >= 8), N >= 32 and N % 8 == 0 (else metal_rows on a clone of dot,
// as gpu_gemm()'s). A block's
// codes' dot is an int32 tile of mma.sync's 16 x 8 int8 tiles (the weights
// the 16-row operand, 32 values of K; 16 where sub-blocks are 16), which each
// lane then scales into dot's float32 tile by the block's factors: per weight
// row (its scale; a two-level weight's sub-block's scale times its block's)
// and per act row (its scale), each a Func (factor_funcs()) read at the
// elements the lane holds. An affine weight's
// (q4_1, q4_K: an offset or min per block) code-free terms are its offset
// times the act's block sum, read from the act's derived port (q8_1.soa's
// sum): two more factors, no multiply. A weight's sub-blocks of 16 values
// (q6_K, q3_K, nvfp4's float scales: `halves`) scale each fragment's tile
// apart, the act's scale their sum.
// Per step of K (the shape's kstep values, or the largest of its halves K is
// a multiple of) the operands are staged in shared memory: the weight's codes
// decoded 4 bytes of a block's run per lane (`code_words`: words of 4 codes,
// in a GPR each; else 16 codes a vector), the act's copied from the kernel
// that encodes them (awaited at the barrier, after the weights' loads), both
// a block's rows 48 bytes apart (16 past the block: 32 puts a tile's rows in
// the same banks) and its blocks a block's rows and `pad_rows` more apart,
// and the factors, dense along N or M. A weight block of several of the
// reduction's (the K-quants' 256 values on 32-value act blocks) is staged
// whole, once (kw), its rows 16 bytes past it, its steps the acts' (a K
// split on mma.sync's tiles splits K in whole ones of it; on wmma's its
// steps are whole ones), or per step (KSplit's wstep). (q4_K at 4096 x 4096
// x 512: its rows 256 + 32 bytes apart and the acts' blocks unpadded, the
// operands' loads and copies 2- and 4-way conflicted, 243 us, 256 + 16 and
// padded 224; on q4_0's K split's tiles from M = 64, against 32 x 64 ones
// on whole weight blocks, 54 + 5.4 -> 41.5 + 2.9 us at M = 64, 80 -> 60 +
// 5.8 at 128)
// Under each of `splits`, first first (where its cond holds and K is a
// multiple of kz steps of its shapes' kstep), K is split, as GGML's
// stream-k: kz gemms, each on every kz-th step of K (dot's
// rfactor, part: a partial sum per step index mod kz; dot's K rounded down to
// whole kz steps, round_extent), in one kernel (pg:
// part's copy, its blocks over n, m and the partial sums; the last in m
// rounded up, its rows past M on the act's last row), then out their sums. A block's steps bound its time, so few tiles gain from kz times the
// blocks. The next split, the gemm on all of K and the rows are then clones'
// of dot.
inline void gpu_int_gemm(Func out, Func dot, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs, std::vector<GemmShape> shapes, int sg,
                         bool code_words, std::vector<KSplit> splits = {}, bool wmma = false) {
    const int fn = 16, pad = 16;
    // (a weight block of several blocks is staged whole, once per `kw`,
    // its steps the acts'; on wmma's fragments the K split's steps are
    // whole ones of it)
    auto wsteps_of = [&](const KSplit &sp) { return fmt.per > 1 && !sp.wmma && fmt.per % (sp.shapes[0].kstep / fmt.block) == 0; };
    for (KSplit &sp : splits) {
        const bool whole = fmt.per > 1 && !wsteps_of(sp);
        for (GemmShape &sh : sp.shapes) {
            if (whole) sh.kstep = std::max(sh.kstep, fmt.block * fmt.per);
        }
    }
    RVar kw("kw"), kr("kr");
    Var n = out.args()[0], m = out.args()[1], ni("mm_ni"), mi("mm_mi"), nw("mm_nw"), mw("mm_mw"), nt("mm_nt"), mt("mm_mt"), u("u");
    RVar rx = r.x, ry = r.y, ks("ks"), kb("kb"), ko("ko"), kt("kt"), rxh("rxh"), rxj("rxj");
    Var h("h");
    // (a weight's sub-blocks of a fragment's values: a block's fragments
    // each a tile, with its sub-block's scale)
    const bool halves = fmt.sub == 16 && fmt.block > 16;
    // A gemm's tiles: wmma's 16 x 16 fragments (wm), else mma.sync's 16 x 8
    // (fm rows of M), of fk values of K; mma.sync's a fragment of M inside one
    // of N (t0 inside t1), as its registers hold them
    auto fm_of = [](bool wm) { return wm ? 16 : 8; };
    auto fk_of = [&](bool wm) { return halves || wm ? 16 : 32; };
    auto t0_of = [&](bool wm) { return wm ? VarOrRVar(nt) : VarOrRVar(mt); };
    auto t1_of = [&](bool wm) { return wm ? VarOrRVar(mt) : VarOrRVar(nt); };
    auto tile_order = [&](Stage st, bool wm) { return wm ? st : st.reorder(mt, nt); };
    auto tile_storage = [&](Func f, bool wm) { return wm ? f : f.reorder_storage(m, n); };
    Expr N = out.output_buffer().dim(0).extent(), M = out.output_buffer().dim(1).extent(), nb = r.y.extent();
    std::vector<Func> wc = rs[0].decoded_components(tag::codes), asc = rs[1].decoded_components(tag::scale), ac;
    for (size_t j = 0; j < rs[1].encoded.size(); j++) {
        if (rs[1].encoded_ports[j].tags && rs[1].encoded_ports[j].tags->count(tag::codes)) ac.push_back(rs[1].encoded[j]);
    }
    if (wc.size() != 1 || ac.size() != 1 || asc.size() != 1) throw std::invalid_argument("gpu_int_gemm: one codes component per operand, one scale per act block");
    std::set<std::string> luts;
    for (Func f : decoders(rs)) {
        if (register_lut(rs, f)) luts.insert(f.name());
    }
    // A grid's points (a precomputed table of `point` codes a row): the
    // weights' codes staged a point a word, a load of the table each, its
    // signs applied in the word (iq2_s at 4096 x 4096 x 128 0.167 -> 0.100
    // ms, iq2_xxs at 512 0.315 -> 0.238; a lane's 16 codes a vector: a byte
    // load and a multiply each; a point a vector: 0.125, 0.295. RTX 3090)
    int point = 0;
    for (Func f : decoders(rs, all_but({}), 0, 1, true)) {
        const Tabulate *t = tabulate(rs[0], f, false, true);
        if (t && t->data) point = t->per;
    }
    // (a lut's or a grid's codes: the K split's 64 x 64 tiles on 2 x 2
    // warps of 32 x 32, twice the lanes to decode a step's weights; iq4_nl
    // at 4096 x 4096 x 64 0.064 -> 0.056 ms, iq2_xxs 0.054 -> 0.050; q4_0's
    // on 2 x 1 warps of 32 x 64, 0.046 against 0.049)
    // An affine weight's offsets per 16-value sub-block (q2_K's: a weight
    // and an act factor more per sub-block, `subsums`) take those warps too,
    // and blocks of 64 rows of M at most: 128 take more than 48 KB of
    // shared memory (q2_K at 4096 x 4096 x 64: 0.094 -> 0.063 ms)
    const bool subsums = halves && affine(rs);
    if (!luts.empty() || point || subsums) {
        for (KSplit &sp : splits) {
            for (GemmShape &sh : sp.shapes) {
                if (!sp.wmma && sh.tm == 8 && sh.wm == 1) sh.tm = 4, sh.wm = 2;
            }
        }
    }
    if (subsums) {
        auto narrow = [&](std::vector<GemmShape> &sl, bool wm) {
            sl.erase(std::remove_if(sl.begin(), sl.end(), [&](const GemmShape &sh) { return fm_of(wm) * sh.tm * sh.wm > 64; }), sl.end());
            for (GemmShape &sh : sl) {
                if (!wm && sh.tm == 8 && sh.wm == 1) sh.tm = 4, sh.wm = 2;
            }
        };
        for (KSplit &sp : splits) {
            narrow(sp.shapes, sp.wmma);
        }
        narrow(shapes, wmma);
    }
    // (the K split's 48-row tiles where a weight block is several blocks,
    // affine or a grid's: at 4096 x 4096 x 48 q4_K 0.050 -> 0.044 ms, q6_K
    // 0.061 -> 0.050, q4_1 0.050 -> 0.043, iq1_s 0.049 -> 0.046 against 64
    // rows; q4_0 0.042 -> 0.046, iq4_nl 0.047 -> 0.081, q2_K 0.060 -> 0.062)
    if (!(fmt.per > 1 || affine(rs) || point) || !luts.empty() || subsums) {
        for (KSplit &sp : splits) {
            sp.shapes.erase(std::remove_if(sp.shapes.begin(), sp.shapes.end(), [&](const GemmShape &sh) { return fm_of(sp.wmma) * sh.tm * sh.wm == 48; }), sp.shapes.end());
        }
    }
    // (the act's codes' sums per 16-value sub-block, where a weight's
    // offsets are per sub-block: q2_K's; a port of the act kernel's)
    Func subsum;
    // A gemm: acc's float32 tiles, a block's (blk: its scales times its
    // codes' dot, codes: int32) and its step's operands in shared memory
    struct Gemm {
        Func acc, blk, codes, ws, xs;
        std::vector<Func> tiles, wfs, xfs;  // (tiles: blk to codes)
        Var b;                              // a step's blocks
        bool wmma = false;
        int wstep = 0;  // (the weight's values per staged step, blocks())
    };
    // s (acc's update, or a partial sum's) by its blocks (`by`: its RVars
    // over them, as Vars)
    auto has_var = [](const Func &f, const Var &v) {
        std::vector<Var> a = f.args();
        return std::any_of(a.begin(), a.end(), [&](const Var &x) { return x.same_as(v); });
    };
    auto blocks = [&](Func acc, Stage s, const std::vector<std::pair<RVar, Var>> &by, Func kernel, bool wm, int wstep = 0) {
        Gemm g;
        g.acc = acc;
        g.wmma = wm;
        const int fk = fk_of(wm);
        g.b = by.back().second;
        g.blk = s.rfactor(by);
        Stage bs = g.blk.update();
        bs.eager_inline(decoders(rs, all_but({tag::codes}), 0, 1));
        std::vector<Func> keep = wc;
        for (const auto &v : {ac, asc}) {
            keep.insert(keep.end(), v.begin(), v.end());
        }
        g.tiles = {g.blk};
        Func sums = g.blk;
        Func rest;
        if (affine(rs)) {
            // sums: the codes' terms; the code-free ones read the act's
            // block sums
            FuncVec parts = bs.hoist_invariants({decoders(rs, only({tag::codes}), 0, 1)});
            sums = parts[0];
            g.tiles.push_back(sums);
            if (!parts[1].defined()) throw std::invalid_argument("gpu_int_gemm: an affine weight's code-free terms");
            rest = parts[1];
            if (!halves) {
                if (!read_port(parts[1], rs[1].derived_ports(), g.blk)) {
                    throw std::invalid_argument("gpu_int_gemm: an affine weight's code-free terms read a derived port of the act (its block sums)");
                }
                // (the port's value, read through a Func of the tile's vars, at
                // the block the other terms read: theirs is promised within K)
                for (const auto &[name, f] : Internal::find_direct_calls(g.blk.function())) {
                    if (name != g.blk.name() && name != sums.name() && f.dimensions() == g.blk.dimensions()) bs.eager_inline(Func(f));
                }
                std::vector<Func> ports;
                for (const DerivedPort &p : rs[1].derived_ports()) {
                    ports.push_back(p.stored);
                }
                promised_args(g.blk, ports);
                rest = Func();
            }
        }
        sums.update().eager_inline(without(decoders(rs), keep));
        if (rest.defined()) {
            // (an affine weight's offsets per 16-value sub-block, q2_K's:
            // per sub-block (rh), its offset times the act's codes' sum
            // over it, which the act kernel computes, subsum: a value per
            // act row and sub-block, so an act factor reads it)
            using namespace Internal;
            rest.update().eager_inline(without(decoders(rs), keep));
            rest.update().split(rx, rxh, rxj, fk);
            Func rh = rest.update().rfactor(rxh, h);
            rh.bound(h, 0, fmt.block / fk);
            Func inner = rh.update().hoist_invariants()[0];
            const Add *a = substitute_in_all_lets(inner.update_value()).as<Add>();
            const Call *self = a ? a->a.as<Call>() : nullptr;
            if (!self || self->name != inner.name()) throw std::invalid_argument("gpu_int_gemm: the act's sums over a sub-block as a sum");
            // (the act's codes' sum over a sub-block t of block y of row
            // m: subsum's (y * subs + t, m), the act kernel's)
            const int subs = fmt.block / fk;
            Expr cx, cy, cm;
            int ncalls = 0;
            Expr inc = mutate_with(a->b, [&](auto *self, const Call *c) -> Expr {
                if (c->call_type != Call::Halide || c->name != ac[0].name()) return self->visit_base(c);
                cx = c->args[0], cy = c->args[1], cm = c->args[2];
                ncalls++;
                return Variable::make(c->type, "gq_code");
            });
            auto bare = [](const Expr &e) {
                return simplify(mutate_with(e, [](auto *self, const Call *c) -> Expr {
                    return c->is_intrinsic(Call::promise_clamped) || c->is_intrinsic(Call::likely) ? self->mutate(c->args[0]) : self->visit_base(c);
                }));
            };
            const std::map<std::string, Expr> at0{{rxj.name(), 0}};
            Expr t = substitute(at0, bare(cx)) / fk, y = substitute(at0, cy), mrow = substitute(at0, cm);
            if (ncalls != 1 || !equal(simplify(inc), simplify(cast(inc.type(), Variable::make(ac[0].type(), "gq_code")))) ||
                !can_prove(bare(cx) == t * fk + Variable::make(Int(32), rxj.name())) || expr_uses_var(y, rxj.name()) || expr_uses_var(mrow, rxj.name())) {
                throw std::invalid_argument("gpu_int_gemm: the act's sums over a sub-block as a sum of its codes");
            }
            if (!subsum.defined()) {
                std::vector<Var> ca = ac[0].args();
                Var q("q");
                Expr e;
                for (int k = 0; k < fk; k++) {
                    Expr c = cast<int32_t>(ac[0]((q % subs) * fk + k, q / subs, ca[2]));
                    e = e.defined() ? e + c : c;
                }
                subsum = Func("act_subsums");
                subsum(q, ca[2]) = e;
            }
            const Expr sv = cast(inner.type(), subsum(y * subs + t, mrow));
            Expr &v = rh.function().update(0).values()[0];
            v = mutate_with(substitute_in_all_lets(v), [&](auto *self, const Call *c) -> Expr {
                if (c->call_type != Call::Halide || c->name != inner.name()) return self->visit_base(c);
                std::map<std::string, Expr> at;
                for (size_t i = 0; i < c->args.size(); i++) {
                    at[inner.args()[i].name()] = c->args[i];
                }
                return substitute(at, sv);
            });
            // blk reads rest as its sub-blocks' terms, products distributed
            // over the sums: an outer product of a weight and an act factor
            // each (factor_funcs())
            const Add *ra = substitute_in_all_lets(rh.update_value()).as<Add>();
            if (!ra) throw std::invalid_argument("gpu_int_gemm: the act's sums' terms as a sum");
            const Expr term = ra->b;
            std::function<Expr(const Expr &)> spread = [&](const Expr &e) -> Expr {
                if (const Add *x = e.as<Add>()) return spread(x->a) + spread(x->b);
                if (const Sub *x = e.as<Sub>()) return spread(x->a) - spread(x->b);
                if (const Mul *x = e.as<Mul>()) {
                    Expr p = spread(x->a), q = spread(x->b);
                    if (const Add *y = p.as<Add>()) return spread(y->a * q) + spread(y->b * q);
                    if (const Add *y = q.as<Add>()) return spread(p * y->a) + spread(p * y->b);
                    return p * q;
                }
                return e;
            };
            Expr &bv = g.blk.function().update(0).values()[0];
            bv = spread(mutate_with(substitute_in_all_lets(bv), [&](auto *self, const Call *c) -> Expr {
                if (c->call_type != Call::Halide || c->name != rest.name()) return self->visit_base(c);
                Expr t;
                for (int i = 0; i < fmt.block / fk; i++) {
                    std::map<std::string, Expr> at{{h.name(), i}};
                    for (size_t j = 0; j < c->args.size(); j++) {
                        at[rest.args()[j].name()] = c->args[j];
                    }
                    Expr e = substitute(at, term);
                    t = t.defined() ? t + e : e;
                }
                return t;
            }));
        }
        if (halves) {
            // the act's scale times the block's sum (whole) of its
            // fragments' (half: each its sub-block's scale times its codes'
            // dot)
            Func whole = sums.update().hoist_invariants()[0];
            whole.update().split(rx, rxh, rxj, fk);
            Func half = whole.update().rfactor(rxh, h);
            half.bound(h, 0, fmt.block / fk);
            g.tiles.insert(g.tiles.end(), {whole, half});
            sums = half;
        }
        g.codes = sums.update().hoist_invariants()[0].change_type(Int(32));
        g.tiles.push_back(g.codes);
        for (size_t i = 0; i + 1 < g.tiles.size(); i++) {
            for (Func w : factor_funcs(g.tiles[i], n, m, g.tiles, "w_factor")) {
                g.wfs.push_back(w);
            }
            for (Func x : factor_funcs(g.tiles[i], m, n, g.tiles, "x_factor")) {
                g.xfs.push_back(x);
            }
        }
        Func wcodes = wc[0];
        if (wstep) {
            // (the weight's codes per `wstep` values of its block: wv's
            // (v, s, row) is the block's codes at its step s, v within
            // it. The codes read the weight's at (x + (y % per) * block, y
            // / per) of the act's (x, y), so wv's at (x + (y % sb) *
            // block, y / sb), sb the act blocks of a step: the same value
            // (y's digits), whose bounds are a step's alone)
            using namespace Internal;
            const int bw = fmt.block * fmt.per, sb = wstep / fmt.block;
            if (wstep % fmt.block || bw % wstep) throw std::invalid_argument("gpu_int_gemm: a weight step of whole act blocks within a weight block");
            std::vector<Var> a = wc[0].args();
            Func wv(wc[0].name() + "_steps");
            wv(a[0], a[1], a[2]) = wc[0](a[0] + (a[1] % (bw / wstep)) * wstep, a[1] / (bw / wstep), a[2]);
            Expr &v = g.codes.function().update(0).values()[0];
            v = substitute_in_all_lets(v);
            Expr ax, ay;
            visit_with(v, [&](auto *self, const Call *c) {
                if (c->call_type == Call::Halide && c->name == ac[0].name()) ax = c->args[0], ay = c->args[1];
                self->visit_base(c);
            });
            v = mutate_with(v, [&](auto *self, const Call *c) -> Expr {
                if (c->call_type != Call::Halide || c->name != wc[0].name()) return self->visit_base(c);
                if (!ax.defined() || !can_prove(c->args[0] == ax + (ay % fmt.per) * fmt.block && c->args[1] == ay / fmt.per)) {
                    throw std::invalid_argument("gpu_int_gemm: the weight's codes not at the act's k");
                }
                return wv(ax + (ay % sb) * fmt.block, ay / sb, c->args[2]);
            });
            wcodes = wv;
            g.wstep = wstep;
        }
        g.ws = wcodes.in(g.codes), g.xs = ac[0].in(g.codes);
        if (point) {
            // (a grid's codes: the weight's codes inlined into their staged
            // copy, a sign's multiply x * (1 - y * 2), y in [0, 1], as (x ^
            // -y) + y, so that a point's codes decode in a GPR: staging()'s)
            using namespace Internal;
            Expr &v = g.ws.function().definition().values()[0];
            for (;;) {
                FunctionPtr fp;
                visit_with(v, [&](auto *self, const Call *c) {
                    if (c->call_type == Call::Halide && c->func.defined() && !fp.defined() && (has(rs[0].decode_funcs(), c->name) || c->name == wcodes.name())) fp = c->func;
                    self->visit_base(c);
                });
                if (!fp.defined()) break;
                v = inline_function(v, Function(fp));
            }
            v = mutate_with(simplify(substitute_in_all_lets(v)), [&](auto *self, const Mul *op) -> Expr {
                const Sub *s = op->b.as<Sub>();
                const Mul *m = s ? s->b.as<Mul>() : nullptr;
                if (s && m && is_const_one(s->a) && is_const(m->b, 2)) {
                    Expr y = m->a;
                    Interval b = bounds_of_expr_in_scope(y, Scope<Interval>::empty_scope());
                    if (b.is_bounded() && can_prove(b.min >= 0 && b.max <= 1)) return (self->mutate(op->a) ^ (make_zero(y.type()) - y)) + y;
                }
                return self->visit_base(op);
            });
        }
        // the weights' luts the gemm's own, per block (as gpu_staged()'s),
        // or with code_words inlined: a lookup of a word's 4 codes in a GPR,
        // byte permutes of the lut's words (iq4_nl at 4096 x 4096 x 16 /
        // 512 0.036 / 0.245 -> 0.025 / 0.218 ms; a byte load of the shared
        // lut per code, 16 codes a lane. RTX 3090)
        for (Func f : clone_paths(g.ws, [&](const Func &f) { return luts.count(f.name()) > 0; })) {
            if (!code_words) metal_lut(f, kernel, n);
        }
        return g;
    };
    auto tile = [&](Stage st, const GemmShape &sh, bool wm) {
        const int fm = fm_of(wm);
        return tile_order(st.tile(n, m, nw, mw, ni, mi, fn * sh.tn, fm * sh.tm).tile(ni, mi, nt, mt, fn, fm), wm).gpu_threads(nw, mw).unroll(ni).unroll(mi).vectorize(nt).vectorize(mt);
    };
    // The kernel's stage so under a shape: its blocks' tiles (over n, m and
    // zs; the last in m rounded up where so is the K split's partial sums,
    // whose rows past M read the last act row: else shifted), acc's tiles
    // per warp
    auto kernel_tiles = [&](Stage so, const GemmShape &sh, Gemm g, const std::vector<VarOrRVar> &zs) {
        const int fm = fm_of(g.wmma), bn = fn * sh.tn * sh.wn, bm = fm * sh.tm * sh.wm;
        so.tile({n, m}, {n, m}, {ni, mi}, {bn, bm}, {TailStrategy::ShiftInwards, zs.empty() ? TailStrategy::ShiftInwards : TailStrategy::RoundUp});
        tile_order(so.tile(ni, mi, nw, mw, ni, mi, fn * sh.tn, fm * sh.tm).tile(ni, mi, nt, mt, fn, fm), g.wmma);
        zs.empty() ? so.gpu_blocks(n, m) : so.gpu_blocks(n, m, zs[0]);
        so.gpu_threads(nw, mw).unroll(ni).unroll(mi).vectorize(nt).vectorize(mt);
        tile(Stage(g.acc).specialize(sh.cond), sh, g.wmma);
    };
    // The step's operands under a shape: each lane 4 bytes of a block of a
    // row of the step's codes (a word of 4 codes of each nibble half; else 16
    // codes of it), or 16 bytes of the act's; the scales
    auto staging = [&](const GemmShape &sh, Gemm g) {
        // st's loop l over the block's lanes (ln) and warps (sw, sm: dot's
        // nw, mw), lt the rest, outside `inner`
        auto lanes = [&](Stage st, Var l, Var lt, std::vector<VarOrRVar> inner) {
            Var ln("ln"), sw("sw"), sm("sm");
            st.split(l, l, ln, 32, TailStrategy::GuardWithIf).split(l, lt, sw, sh.wn * sh.wm, TailStrategy::GuardWithIf);
            if (sh.wm > 1) st.split(sw, sm, sw, sh.wn);
            inner.insert(inner.end(), {lt, ln, sw});
            if (sh.wm > 1) inner.push_back(sm);
            st.reorder(inner).gpu_lanes(ln).unroll(lt);
            sh.wm > 1 ? st.gpu_threads(sw, sm) : st.gpu_threads(sw);
        };
        for (Func f : {g.ws, g.xs}) {
            std::vector<Var> a = f.args();
            Var l("l"), lt("lt"), jv("jv"), jh("jh"), jw("jw"), jb("jb");
            Stage st = f.specialize(sh.cond);
            if (f.name() == g.ws.name() && code_words) {
                // (a lane 4 bytes of a run of bytes' two nibbles, a word of
                // 4 codes of each, the run's words on consecutive lanes: a
                // block's 32 values of 16 bytes on 4 lanes; of a weight
                // block of several blocks, 64 of 32 on 8, each lane every
                // run, jb, of its block: their high bits' positions differ
                // by run. A lane a block's whole run, its rows' 2-byte
                // loads 18 bytes apart across the lanes: q4_0 at 4096 x
                // 4096 x 512 on wmma 0.278 -> 0.262 ms, M = 128 0.092 -> 0.083)
                const int run = fmt.per > 1 ? 32 : 16;
                st.split(a[0], jb, jv, 2 * run).split(jv, jh, jv, run).split(jv, jw, jv, 4).fuse(jw, a[1], lt).fuse(lt, a[2], l);
                lanes(st, l, lt, {jv, jh, jb});
                st.vectorize(jv, 4, VectorizeIn::SWAR).unroll(jh).unroll(jb);
            } else {
                if (point && f.name() == g.ws.name()) {
                    // (a lane a grid point's codes per 1 / np of the
                    // block, np points: each a word in a GPR (SWAR), one
                    // load of the table; the lanes' points consecutive)
                    Var jo("jo"), jr("jr");
                    const int e = g.wstep ? g.wstep : fmt.block * fmt.per, np = 16 / point;
                    st.split(a[0], jo, jr, e / np).split(jr, l, jv, point).fuse(l, a[1], lt).fuse(lt, a[2], l);
                    lanes(st, l, lt, {jv, jo});
                    st.vectorize(jv, point, VectorizeIn::SWAR).unroll(jo);
                } else {
                    st.split(a[0], l, jv, 16).fuse(l, a[1], lt).fuse(lt, a[2], l);
                    lanes(st, l, lt, {jv});
                    st.vectorize(jv);
                }
            }
        }
        for (bool w : {true, false}) {
            for (Func f : w ? g.wfs : g.xfs) {
                // (the act's: along the row, then the step's blocks, as
                // they are stored; the weight's: the step's blocks, then
                // along the row, as its records are: q4_0 at 4096 x 4096 x
                // 128 0.083 -> 0.076 ms, along the row; but a weight block
                // of several blocks' on wmma's tiles along the row: q4_K at
                // M = 64 0.075 -> 0.067, iq2_s at 32 0.105 -> 0.077, where
                // on mma.sync's q4_K at 512 0.336 -> 0.279; the vars outside
                // them one value per step)
                std::vector<Var> a = f.args();
                std::vector<VarOrRVar> order;
                const bool blocks_inner = w && (fmt.per == 1 || !g.wmma);
                if (!blocks_inner) order.push_back(a.back());
                order.insert(order.end(), a.begin(), a.end() - 1);
                if (blocks_inner) order.push_back(a.back());
                Stage st = f.specialize(sh.cond);
                st.reorder(order);
                Var l = order[0].var, lt("lt");
                for (size_t i = 1; i < order.size(); i++) {
                    Var fl("l" + std::to_string(i));
                    st.fuse(l, order[i].var, fl);
                    l = fl;
                }
                lanes(st, l, lt, {});
            }
        }
    };
    // acc's tiles per block of `kernel`, a block's tiles (the codes' dot and
    // its scaled copy) and the step's operands
    auto storage = [&](Gemm g, Func kernel, const std::vector<GemmShape> &shapes_of, Expr blocks_per_step = Expr(), bool wsteps = false) {
        // (a tile's storage as its loops: a fragment of M within one of N)
        const bool wm = g.wmma;
        const int fm = fm_of(wm);
        const VarOrRVar t0 = t0_of(wm), t1 = t1_of(wm);
        tile_storage(g.acc.compute_at(kernel, n).store_in(MemoryType::Tile), wm);
        for (Func f : g.tiles) {
            tile_storage(f.compute_at(g.acc, kb).store_in(MemoryType::Tile), wm);
            for (int i = 0; i <= f.num_update_definitions(); i++) {
                Stage st = i ? f.update(i - 1) : Stage(f);
                tile_order(st.tile(n, m, ni, mi, nt, mt, fn, fm), wm).unroll(ni).unroll(mi).vectorize(nt).vectorize(mt);
                if (has_var(f, h)) st.unroll(h);
                if (i && f.rvars(i - 1).size() == 1 && f.rvars(i - 1)[0].name() == rxh.name()) st.reorder(t0, t1, ni, mi, rxh).unroll(rxh);
                if (i) st.atomic();
            }
        }
        g.codes.update().split(halves ? rxj : rx, ko, kt, fk_of(wm)).reorder(kt, t0, t1, ni, mi, ko).unroll(ko).vectorize(kt);
        for (Func f : {g.ws, g.xs}) {
            std::vector<Var> a = f.args();
            const bool w = f.name() == g.ws.name();
            f.compute_at(g.acc, w && wsteps && !g.wstep ? kw : ks).store_in(w ? MemoryType::GPUShared : MemoryType::GPUSharedAsync);
            f.reorder_storage(a[0], a[2], a[1]);
            // (a weight block of several blocks: its row 16 bytes past it,
            // as a block's)
            if (w && fmt.per > 1) {
                f.bound_storage(a[0], (g.wstep ? g.wstep : fmt.block * fmt.per) + pad);
            } else {
                f.align_storage(a[0], fmt.block + pad);
            }
            // (a block's rows and pad_rows more: the lanes of a copy or a
            // store span the step's blocks, which a multiple of 8 rows of
            // 48 bytes puts in the same banks, 4-way conflicted; q4_0 at
            // 4096 x 4096 x 512, 64 x 128 tiles, 203-213 -> 185 us. A
            // weight block of several blocks is one per step)
            if (fmt.per == 1 || !w) {
                Expr rows;
                for (auto it = shapes_of.rbegin(); it != shapes_of.rend(); ++it) {
                    const int r = (w ? fn * it->tn * it->wn : fm * it->tm * it->wm) + it->pad_rows;
                    rows = rows.defined() && it->cond.defined() ? select(it->cond, r, rows) : Expr(r);
                }
                f.bound_storage(a[2], rows);
            }
            // (a weight block of several blocks: whole ones per step)
            if (blocks_per_step.defined()) f.bound_extent(a[1], w ? max(1, blocks_per_step / ((g.wstep ? g.wstep : fmt.block * fmt.per) / fmt.block)) : blocks_per_step);
        }
        for (const auto &v : {g.wfs, g.xfs}) {
            for (Func f : v) {
                std::vector<Var> a = f.args(), order{a.back()};
                order.insert(order.end(), a.begin(), a.end() - 1);
                f.compute_at(g.acc, ks).store_in(MemoryType::GPUShared).reorder_storage(order);
                if (blocks_per_step.defined()) f.bound_extent(g.b, blocks_per_step);
            }
        }
    };
    // acc's update per step (ks) of blocks (kb)
    auto steps = [&](Stage st, bool wm, bool wsteps = false) {
        std::vector<VarOrRVar> order{t0_of(wm), t1_of(wm), ni, mi, kb, nw, mw, ks};
        if (wsteps) order.push_back(kw);
        st.reorder(order).unroll(kb).atomic();
    };
    // (the K split's tiles rounded up in M: from M = 8)
    Expr cond = M >= 16 && N >= 32 && N % 8 == 0, split_cond = M >= 8 && N >= 32 && N % 8 == 0;
    // out reads dot through a wrapper; the K split takes dot, the gemm on
    // all of K and the rows clones of it (as gpu_gemm()'s rows); a gemm's
    // luts its own, per block of its kernel (blocks())
    Func via = dot.in(out);
    for (size_t i = 0; i < splits.size(); i++) {
        const int kz = splits[i].kz;
        const std::vector<GemmShape> &split_shapes = splits[i].shapes;
        const std::string sfx = std::to_string(i);
        const bool sw = wsteps_of(splits[i]);
        const int st = split_shapes[0].kstep / fmt.block, kst = sw ? fmt.per : st;
        Expr whole = nb % (kz * kst) == 0;
        Var z("kz_z" + sfx), uo("uo"), ui("ui"), uw("uw"), no("kz_no" + sfx), nj("kz_ni" + sfx);
        RVar kso("kso"), kzr("kzr");
        Stage sk = Stage(out).specialize(split_cond && splits[i].cond && whole).eager_inline(via);
        Func all = dot.clone_in(via);
        // (dot's K whole kz steps: out reads it only under sk, where K is)
        round_extent(dot, ry, kz * kst, out);
        // ry = (ks * kz + kzr) * st + kb; sw: a weight block of several
        // blocks the split's unit, ry = (kw * kz + kzr) * per + kr, kr
        // its steps (as the gemm on all of K's)
        Stage su = dot.update();
        if (sw) {
            su.split(ry, kso, kr, fmt.per).split(kso, kw, kzr, kz);
        } else {
            su.split(ry, kso, kb, st).split(kso, ks, kzr, kz);
        }
        Func part = su.rfactor(kzr, z);
        Func pg = part.in(dot);
        Stage pu = part.update();
        Gemm g = sw ? blocks(part, pu, {{kw, uw}, {kr, ui}}, pg, splits[i].wmma, splits[i].wstep ? st * fmt.block : 0) : blocks(part, pu, {{ks, uo}, {kb, ui}}, pg, splits[i].wmma);
        pg.compute_at(out, Var::outermost());
        for (const GemmShape &sh : split_shapes) {
            Expr c = sh.cond.defined() ? sh.cond : Internal::const_true();
            kernel_tiles(Stage(pg).specialize(c), sh, g, {z});
            if (sw) {
                steps(tile(pu.specialize(sh.cond), sh, g.wmma).split(kr, ks, kb, st), g.wmma, true);
            } else {
                steps(tile(pu.specialize(sh.cond), sh, g.wmma), g.wmma);
            }
            staging(sh, g);
        }
        // (a step's blocks: dot's K is whole kz steps, which the bounds of
        // the partial sums' blocks, clamped to it, don't show)
        storage(g, pg, split_shapes, st, sw);
        sk.split(n, no, nj, 128, TailStrategy::GuardWithIf).gpu_blocks(no, m).gpu_threads(nj);
        dot.compute_at(out, nj);
        su.unroll(kzr);
        dot = all;
    }
    Stage mm = Stage(out).specialize(cond).eager_inline(via);
    Func rows = dot.clone_in(via);
    out.eager_inline(via);
    Gemm g = blocks(dot, dot.update(), {{ry, u}}, out, wmma);
    std::vector<std::pair<Expr, int>> step_blocks;
    for (const GemmShape &sh : shapes) {
        Expr c = sh.cond.defined() ? sh.cond : Internal::const_true();
        kernel_tiles(mm.specialize(c), sh, g, {});
        // the steps of K: ks; kb: their blocks
        Stage su = dot.update().specialize(sh.cond);
        if (fmt.per > 1) {
            // (K is whole weight blocks: each staged once, kw, its steps the
            // acts')
            const int st = std::min(sh.kstep / fmt.block, fmt.per);
            steps(tile(su, sh, wmma).split(ry, kw, kr, fmt.per).split(kr, ks, kb, st), wmma, true);
            step_blocks.emplace_back(c, st);
        } else {
            for (int st = sh.kstep / fmt.block; st >= 1; st /= 2) {
                Stage s = st > 1 ? su.specialize(nb % st == 0) : su;
                steps(tile(s, sh, wmma).split(ry, ks, kb, st), wmma);
            }
        }
        staging(sh, g);
    }
    Expr per_step;
    for (auto it = step_blocks.rbegin(); it != step_blocks.rend(); ++it) {
        per_step = per_step.defined() ? select(it->first, it->second, per_step) : Expr(it->second);
    }
    storage(g, out, shapes, per_step, fmt.per > 1);
    metal_luts(out, rs);
    metal_rows(out, rows, r, fmt.block, rs, fmt.sub, true, true, sg);
    // the acts, encoded by one kernel of 16 blocks a GPU block: the codes 4
    // values a thread, the scales (dense along M: the gemms' act factors
    // decode them as they stage them, as the rows do) and a derived port
    // (q8_1's sum) a thread per block, each block's maximum staged once
    // (q4_0 at 4096 x 4096 x 512: a kernel each 14.7 + 14.3 us, one 16.4;
    // of 8 blocks 18.0, 32 17.1; RTX 3090). The reductions (the scale's
    // maximum, the codes' sum) of vectors.
    Var bo("bo"), jv("jv"), row("enc_row"), blk("enc_blk");
    std::vector<Func> enc = encoder_funcs(rs[1].encoded[0], rs);
    auto calls = [](const Func &f, const std::string &g) { return Internal::find_direct_calls(f.function()).count(g) > 0; };
    // A fused group is computed whole or not at all, and the rows read no
    // derived port: they read their own copies of the encoder (each Func
    // of it they reach, once its callers there are copied), its own kernel
    std::map<std::string, Func> rc;
    if (!rs[1].derived_ports().empty() || subsum.defined()) {
        std::vector<Func> path{rows}, todo;
        for (const auto &[name, f] : Internal::find_transitive_calls(rows.function())) {
            (std::any_of(enc.begin(), enc.end(), [&](const Func &x) { return x.name() == name; }) ? todo : path).push_back(Func(f));
        }
        while (!todo.empty()) {
            auto ready = std::find_if(todo.begin(), todo.end(), [&](const Func &g) {
                return std::none_of(todo.begin(), todo.end(), [&](const Func &h) { return h.name() != g.name() && calls(h, g.name()); });
            });
            if (ready == todo.end()) throw std::invalid_argument("gpu_int_gemm: the acts' encoder is not a DAG");
            std::vector<Func> callers;
            for (const Func &f : path) {
                if (calls(f, ready->name())) callers.push_back(f);
            }
            for (const auto &[name, f] : rc) {
                if (calls(f, ready->name())) callers.push_back(f);
            }
            rc[ready->name()] = Func(*ready).clone_in(callers);
            todo.erase(ready);
        }
    }
    // a derived port reads the other ports' values, as a copy of each in
    // its encoder (not their kernel's output: it is the same kernel)
    std::vector<Func> ports, derived;
    for (size_t j = 0; j < rs[1].encoded.size(); j++) {
        ports.push_back(rs[1].encoded[j]);
        const std::vector<DerivedPort> &dp = rs[1].derived_ports();
        if (std::any_of(dp.begin(), dp.end(), [&](const DerivedPort &p) { return p.name == rs[1].encoded_ports[j].name; })) derived.push_back(rs[1].encoded[j]);
    }
    if (subsum.defined()) derived.push_back(subsum);
    for (const Func &d : derived) {
        std::vector<Func> chain{d};
        for (const auto &[name, f] : Internal::find_transitive_calls(d.function())) {
            if (std::any_of(enc.begin(), enc.end(), [&](const Func &x) { return x.name() == name; })) chain.push_back(Func(f));
        }
        for (const Func &q : ports) {
            if (q.name() == d.name()) continue;
            std::vector<Func> callers;
            for (const Func &f : chain) {
                if (f.name() != q.name() && calls(f, q.name())) callers.push_back(f);
            }
            if (!callers.empty()) Func(q).clone_in(callers);
        }
    }
    std::vector<Func> copies;
    auto schedule_acts = [&](Func codes, const std::vector<Func> &fused, const std::function<Func(const Func &)> &of) {
        std::vector<Var> a = codes.args();
        codes.compute_at(out, Var::outermost()).rename(a[1], blk).rename(a[2], row).split(a[0], a[0], jv, 4).split(blk, bo, blk, 16, TailStrategy::GuardWithIf).fuse(a[0], blk, a[0]).vectorize(jv).gpu_blocks(bo, row).gpu_threads(a[0]);
        for (Func e : fused) {
            a = e.args();
            // (the sub-block sums: the sub-blocks of bo's 16 blocks)
            const int per_bo = subsum.defined() && e.name() == subsum.name() ? 16 * (fmt.block / 16) : 16;
            e.compute_at(out, Var::outermost()).reorder_storage(a[1], a[0]).rename(a[0], blk).rename(a[1], row).split(blk, bo, blk, per_bo, TailStrategy::GuardWithIf).gpu_blocks(bo, row).gpu_threads(blk).compute_with(codes, bo);
        }
        for (const Func &g : enc) {
            Func f = of(g);
            if (!f.defined() || !f.has_update_definition() || f.dimensions() >= codes.dimensions()) continue;
            a = f.args();
            f.compute_at(codes, bo).gpu_threads(a[0]);
            f.update().gpu_threads(a[0]);
            copies.push_back(f);
        }
    };
    std::vector<Func> fused;
    for (const Func &e : ports) {
        if (e.name() != ac[0].name()) fused.push_back(e);
    }
    if (subsum.defined()) fused.push_back(subsum);
    schedule_acts(ac[0], fused, [](const Func &g) { return g; });
    if (rc.count(ac[0].name())) {
        std::vector<Func> rfused;
        for (const Func &e : fused) {
            if (rc.count(e.name())) rfused.push_back(rc[e.name()]);
        }
        schedule_acts(rc[ac[0].name()], rfused, [&](const Func &g) { return rc.count(g.name()) ? rc[g.name()] : Func(); });
    }
    for (Func f : copies) {
        if (f.has_update_definition() && f.update().is_order_independent()) f.update().atomic(true).vectorize(f.rvars()[0]);
    }
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
// (dots[0]). `x`: the act dots[0] reads (X or its round trip), unless staged.
inline void mul_mat(Func out, const std::vector<Func> &dots, const RDom &r, const Formats &fmt, const std::vector<ApproximationResult> &rs,
                    const std::vector<Func> &staged, Func x, const Target &t, const Approximation &acc = Approximation(), const Knobs &k = Knobs()) {
    luts(rs, !t.has_gpu_feature(), t);
    // GPUs: Metal's schedules, on CUDA with wmma's fragments (a warp is a
    // simdgroup: 32 lanes). Metal's matrix unit: 8 x 8 fragments of f16 or
    // f32, from M = 8. CUDA's (wmma): 16 x 16 fragments of f16 or bf16 (none
    // of f32), blocked weights from M = 16 (q4_K:f16 at 4096 x 4096 x 16 113
    // us on the rows, 64 on the gemm; iq2_xs:f16 131, 62; RTX 3090), flat
    // ones from 2 (f32: 8) on guarded tiles (cuda_shapes()), where N % 8 == 0
    // (gpu_gemm's n_align). The rows: 2 warps (simdgroups) per block on
    // Metal, 1 on CUDA (integer rows: 2).
    if (t.has_feature(Target::Metal) || t.has_feature(Target::CUDA)) {
        const bool metal = t.has_feature(Target::Metal);
        const std::string gpu = metal ? "metal: " : "cuda: ";
        const int sg = metal ? 2 : 1;
        if (acc.defined()) throw std::invalid_argument(gpu + "dots accumulated in f32");
        if (fmt.act_rows > 1 || fmt.rows > 1) throw std::invalid_argument(gpu + "weights and acts one row per record");
        Func wh, xh;
        if (!metal && f16_operands(rs, staged, fmt, wh, xh, true)) {
            // CUDA stages 16 bytes per lane, the rows 8 values apart from
            // the banks' period; f16 and bf16 operands as stored are copied,
            // acts encoded here (f32:f16) from a kernel of their own
            // (blocked weights: the acts' copies plain loads, super-blocks'
            // weights 16 values a lane on the one-fragment tiles)
            const Staging cs{8, 8, !fmt.wcompute, staged.empty(), !staged.empty(), fmt.block == 1, fmt.block > 1};
            // The flat gemm from M * N = 2^13 (f16 x f16 at 4096 x 1024 x 8
            // 0.028 ms, the rows 0.031; x 32 0.035, 0.111; 2048 x 2048 x 4
            // 0.018, 0.020; at 4096 x 1024 x 4 the rows 0.022), f32 weights'
            // guarded tiles (M < 16) from 2^15 and M = 8 (f32:f16 at 4096 x
            // 4096 x 8 0.101, the rows 0.129, x 4 0.101, 0.089; 4096 x 1024 x
            // 8 0.058, 0.046; 2048 x 512 x 16 0.017, 0.024). (RTX 3090)
            Expr M = out.output_buffer().dim(1).extent(), MN = cast<int64_t>(M) * out.output_buffer().dim(0).extent();
            Expr large = cs.wcopy ? MN >= (1 << 13) : (M >= 16 && MN >= (1 << 13)) || (M >= 8 && MN >= (1 << 15));
            gpu_gemm(out, dots[0], wh, xh, r, fmt, rs, fmt.block == 1 ? 2 : 16, cuda_shapes(out, cs.wcopy, fmt.block, k.cuda_one_warp), 16, staged, 8, sg, cs, large);
            return;
        }
        if (metal && (f16_operands(rs, staged, fmt, wh, xh) || f32_operands(dots[0], x, rs, staged, fmt, wh, xh))) {
            gpu_gemm(out, dots[0], wh, xh, r, fmt, rs, 8, metal_shapes(out, wh, fmt), 8, staged);
            return;
        }
        if (fmt.block == 1) {
            metal_flat(out, dots[0], r, staged, sg);
            return;
        }
        const bool integer = !metal && fmt.act_block == fmt.block;
        if (fmt.wcompute || (!staged.empty() && !integer)) throw std::invalid_argument(gpu + "blocked weights on acts stored as computed (CUDA: but integer ones), or a weight compute format without f16 acts");
        // CUDA, acts quantized here to planar fields (f32:q8_0.soa, f32:q8_1.soa): the
        // integer gemm, K split in 4 below M = 64 and in 2 below 256
        // (cuda_int_splits())
        if (!staged.empty() && rs.size() == 2 && rs[1].encoded.size() > 1) {
            const std::string &it = k.int_tiles;
            std::string t = it;
            if (it.size() == 3) t = {it[0], it[0], it[1], it[1], it[2]};
            if (it.size() == 4) t = {it[0], it[1], it[2], it[2], it[3]};
            if (t.size() != 5 || t.find_first_not_of("mw") != std::string::npos) throw std::invalid_argument("int_tiles: 3 to 5 of w and m, not " + it);
            gpu_int_gemm(out, dots[0], r, fmt, rs, cuda_int_shapes(out, t[4] == 'w'), 2, k.code_words, cuda_int_splits(out, t[0] == 'w', t[1] == 'w', t[2] == 'w', t[3] == 'w'), t[4] == 'w');
            return;
        }
        // CUDA: acts quantized here (f32:q8_1, as GGML's mmvq quantizes its
        // f32 acts) by a kernel of their own, a thread per record,
        // its block's reductions (the scale's maximum) once per record
        // (inlined, each is a loop inside the next's and the codes'), the
        // order-independent ones as vector reductions: trees, not chains of
        // the block's length
        for (Func e : staged) {
            Var b = e.args()[0], rec = e.args().back(), bo("bo");
            e.compute_root().split(b, bo, b, 32, TailStrategy::GuardWithIf).gpu_blocks(bo, rec).gpu_threads(b);
            for (Func f : encoder_funcs(e, rs)) {
                if (f.name() == e.name() || !f.has_update_definition() || f.dimensions() > e.dimensions()) continue;
                f.compute_at(e, b);
                if (f.update().is_order_independent()) f.update().atomic(true).vectorize(f.rvars()[0]);
            }
        }
        metal_luts(out, rs);
        metal_rows(out, dots[0], r, fmt.block, rs, fmt.sub, true, integer, integer ? 2 : sg);
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
            acc.defined() ? flat_tree(out, 0, dots[0], c, ts, r, acc, staged) : flat(out, 0, dots[0], c, ts, r, t, bf16_pairs(rs, t));
        }
        return;
    }
    Tiles mmla8{4, 8, 1, 32, 32, true}, gemm{4, 4, 1, 32, 32, true}, pairs{2, 1, 2, 16}, rows{1, 1, 2, 16};
    bool mmla_ok = integer && t.has_feature(Target::ARMI8MM);
    // AMX: integer weights of one row per record (blocked_amx(); with sub-blocks, 8 <= M < 16 alone,
    // but in AMX's B tile layout: amx_recs)
    Tiles amx{16, 16, 1, 32, 32, true}, amx4{16, 64, 1, 32, 64, true}, amx8{16, 8, 1, 32, 32, true},
        amx2{16, 32, 1, 32, 32, true};
    bool amx_ok = integer && t.has_feature(Target::AVX512_SapphireRapids) && !whole && fmt.rows == 1 && fmt.per == 1 &&
                  fmt.block % 4 == 0 && (fmt.block <= 64 || fmt.block % 64 == 0);
    // (records of 16 rows in 4-byte pieces: AMX's B tile layout, as GGML's
    // AMX buffer: M >= 16, and M = 8; the rest on the rows path, its gemv
    // 1.3-1.6x GGML's AMX at M = 1, a 16-row tile op slower below M = 8;
    // blocks of 32, q4_0's, as amx_ok: a tile op per block)
    bool amx_recs = integer && t.has_feature(Target::AVX512_SapphireRapids) && !whole && fmt.rows == 16 && fmt.chunk == 4 && fmt.per == 1 &&
                    (fmt.block <= 64 || fmt.block % 64 == 0);
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
        blocked(out, 1, dr, odd_n, {1, tiled ? t16.mt : gemm.mt, 1, 32, 32, true}, r, fmt.block, integer, rs, t);
        Expr mt = t12.mt;
        for (size_t i = tiles.size() - 1; i-- > 0;) {
            mt = select(tiles[i].first, tiles[i].second.mt, mt);
        }
        dr.bound_extent(dr.args()[1], tiled ? select(odd_n, t16.mt, mt) : Expr(gemm.mt));
    }
    if (whole && tiled && !rmmla) {
        for (auto [c, ts] : tiles) {
            lanes ? blocked_rows(out, whole, dr, c, ts, r, fmt.block, fmt.chunk, rs, fmt.act_rows) : blocked_mmla(out, whole, dr, c, ts, r, fmt.block, rs, t, false, 0, 0, false, false, fmt.sub ? 1 : fmt.per);
        }
    } else if (rmmla) {
        // whole records of rows (from 0 mod their rows: their addresses dense)
        // (dr's act is the last result: its derived ports, rs[1]'s for derived_sums)
        std::vector<ApproximationResult> rr = rs;
        if (whole) std::swap(rr[1], rr.back());
        blocked_mmla(out, whole, dr, whole ? Expr() : fits(rm, M), rm, r, fmt.block, rr, t, true, fmt.sub, fmt.act_sub, true, whole, 1, k);
    } else if (mmla_ok) {
        blocked_mmla(out, whole, dr, fits(mmla8, M), mmla8, r, fmt.block, rs, t, true, fmt.sub, fmt.act_sub, false, false, 1, k);
    } else if (amx_recs) {
        // (8 < M < 16: the rows path; two 8-row tiles would overlap)
        blocked_amx(out, 0, dr, fits(amx4, M), amx4, r, fmt.block, rs, t, fmt.sub, fmt.act_sub, true);
        blocked_amx(out, 0, dr, fits(amx2, M), amx2, r, fmt.block, rs, t, fmt.sub, fmt.act_sub, true);
        blocked_amx(out, 0, dr, fits(amx, M), amx, r, fmt.block, rs, t, fmt.sub, fmt.act_sub, true);
        blocked_amx(out, 0, dr, fits(amx8, M) && M % 8 == 0, amx8, r, fmt.block, rs, t, fmt.sub, fmt.act_sub, true);
    } else if (amx_ok && !fmt.sub) {
        blocked_amx(out, 0, dr, fits(amx4, M), amx4, r, fmt.block, rs, t);
        blocked_amx(out, 0, dr, fits(amx, M), amx, r, fmt.block, rs, t);
        blocked_amx(out, 0, dr, fits(amx8, M), amx8, r, fmt.block, rs, t);
    }
    // AVX-512 VNNI: blocks of 256 (GGML's tiled gemm's formats) on tiles
    // vectorized across M (blocked_cols())
    Tiles colsM{64, 256, 1, 64, 256, true}, cols32{8, 32, 1, 64, 32, true}, cols16{8, 16, 1, 64, 16, true}, cols8{8, 8, 1, 64, 8, true};
    bool cols_ok = integer && !(amx_ok && !fmt.sub) && !tiled && !whole && fmt.rows == 1 && fmt.per == 1 && fmt.block == 256 &&
                   t.has_feature(Target::AVX512_SapphireRapids) && staged.size() <= 1;
    if (cols_ok) {
        blocked_cols(out, 0, dr, fits(colsM, M), colsM, r, fmt.block, rs, t, fmt.sub, fmt.act_sub, k);
        blocked_cols(out, 0, dr, fits(cols32, M), cols32, r, fmt.block, rs, t, fmt.sub, fmt.act_sub, k, false);
        blocked_cols(out, 0, dr, fits(cols16, M), cols16, r, fmt.block, rs, t, fmt.sub, fmt.act_sub, k, false);
        // (sub-blocks on AMX, as GGML's AMX K-quant kernels, if each fills
        // half a tile op's K at least: 16 values, twice the tile ops and
        // rescales per block, are slower than cols8: q6_K, q2_K 1.2-1.3x)
        if (amx_ok && fmt.sub >= 32) {
            blocked_amx(out, 0, dr, fits(amx8, M), amx8, r, fmt.block, rs, t, fmt.sub, fmt.act_sub);
        } else {
            blocked_cols(out, 0, dr, fits(cols8, M), cols8, r, fmt.block, rs, t, fmt.sub, fmt.act_sub, k, false);
        }
    }
    int wper = t.arch == Target::X86 && !fmt.sub && integer ? fmt.per : 1;
    // (records without i8mm: their gemv per column; blocked()'s tiles gather)
    if (!(recs && !whole) && (!whole || !tiled)) blocked(out, whole, dr, whole ? Expr() : fits(gemm, M), gemm, r, fmt.block, integer, rs, t, !whole, fmt.sub, fmt.act_sub, k, false, wper);
    // The rows past Mr (with whole records, M = 1's): no tasks when there are none.
    Expr rest = whole ? M == 1 : Expr();
    if (integer && fmt.rows > 1) {  // N is a multiple of the records' rows
        Tiles rt{fmt.rows, 1, 2, 16, 0, false, rest};
        // (a two-level record, 8 rows of q4_K: 1152 bytes, one per iteration)
        if (fmt.sub) {
            rt.interleave = 1;
            blocked_rows(out, 2 * whole, dots[0], Expr(), rt, r, fmt.block, fmt.chunk, rs, 0, fmt.sub, fmt.act_sub);
            return;
        }
        // Codes decoded from a field: two records per tile where N allows
        // (gemv 1.02-1.1x; q8_0's, not decoded, 1.02-1.11x slower on ten
        // threads at K = 14336), unless they stream too fast for two
        // (two_streams()).
        if (decoded_codes(rs)) {
            Tiles r2 = rt;
            r2.nt = 2 * fmt.rows;
            r2.rows = std::max(r2.rows, r2.nt);  // (a task a tile at least)
            Expr two = N % r2.nt == 0;
            if (fmt.row_bytes.defined()) two = two && (two_streams(fmt.row_bytes) || threads() > 4);
            blocked_rows(out, 2 * whole, dots[0], two, r2, r, fmt.block, fmt.chunk, rs);
        }
        blocked_rows(out, 2 * whole, dots[0], Expr(), rt, r, fmt.block, fmt.chunk, rs);
        return;
    }
    pairs.busy = rows.busy = rest;
    pairs.interleave = rows.interleave = k.interleave;
    // Staged tables: no row pairs. Codes read as stored (q8_0's): the rows
    // stream too fast for two (two_streams()); decoded codes gain from
    // sharing the act at any row length.
    Expr paired = fits(pairs, 1);
    if (fmt.row_bytes.defined() && !decoded_codes(rs)) paired = paired && two_streams(fmt.row_bytes);
    if (k.row_pairs && staged_tables(rs).empty()) blocked(out, 2 * whole, dots[0], paired, pairs, r, fmt.block, integer, rs, t, true, fmt.sub, fmt.act_sub, k, true, wper);
    blocked(out, 2 * whole, dots[0], Expr(), rows, r, fmt.block, integer, rs, t, true, fmt.sub, fmt.act_sub, k, true, wper);
}

}  // namespace ggml
