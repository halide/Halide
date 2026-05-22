#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

#include "Bounds.h"
#include "CodeGen_Internal.h"
#include "DeviceInterface.h"
#include "ExprUsesVar.h"
#include "FindCalls.h"
#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "InjectHostDevBufferCopies.h"
#include "Profiling.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include "UniquifyVariableNames.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

// =============================================================================
// Profiling injection
// =============================================================================
//
// When a pipeline is compiled with the "profile" target feature, this
// lowering pass instruments the IR with calls into the Halide profiler
// runtime. At run time the profiler then collects per-Func statistics —
// time spent, memory used, points realized, kinds of loads issued, parallel
// loops launched, inlined call counts, and so on — which the runtime prints
// as a report when the pipeline terminates (or makes available as JSON to
// external tools).
//
// Two complementary mechanisms collect data at run time:
//
//  - Sampling. A background profiler thread (started lazily by the runtime
//    on first use) periodically reads a "current func" word that the
//    generated code writes whenever the executing pipeline transitions
//    between Funcs. The distribution of samples across Funcs gives the time
//    profile. This handles wall time cheaply: the only thing the generated
//    code does on the hot path is one store of an integer id.
//
//  - Direct counters. Discrete events that need exact counts (loads, stores,
//    allocations, parallel launches, inlined call sites, ...) can't be
//    reliably sampled, so the generated code calls
//    halide_profiler_update_counters at appropriate points to bump per-Func
//    64-bit counters by computed amounts. The runtime keeps one
//    halide_profiler_func_stats struct per id; the counters live in there.
//
// This pass is responsible for emitting both — the set_current_func writes,
// the counter updates, and all the surrounding scaffolding (instance start
// and stop, thread activity tracking around parallel constructs, sampling
// token management for leaf tasks, stack-peak accounting, etc.).
//
// -----------------------------------------------------------------------------
// Structure of the passes in this file
// -----------------------------------------------------------------------------
//
// This file exposes two top-level passes. resolve_inline_markers runs very
// early in lowering — immediately after schedule_functions — to consume the
// inline_marker intrinsics that Inline.cpp leaves at every inlined call
// site and turn them into stmt-level declare_inlined intrinsics. Doing it
// early lets every later pass safely transform Provides without tripping
// on inline_marker (which has no codegen support). It's the only pass here
// that's not gated on "we're actually instrumenting"; the markers always
// need to be removed if they're there.
//
// inject_profiling is the main pass and runs much later, after bounds
// inference but before storage flattening. It consumes declare_inlined,
// declare_box_required_at_{realization,production,root}, and emits all the
// runtime calls. It runs three sub-passes over the IR:
//
//  1) PreAllocateEntries. Enumerates every entry (see below) for every
//     Func and assigns each one an integer id matching its eventual slot in
//     the runtime's halide_profiler_func_stats array. To save the next pass
//     from re-parsing the inlining graph, it also rewrites each
//     declare_inlined intrinsic to a flat list of the resolved entry ids;
//     the intrinsic's type (carrying the vector lane count) is preserved.
//
//  2) InjectCounters. Walks the IR and accumulates per-entry counter
//     contributions, flushing them as halide_profiler_update_counters calls.
//     Counters are hoisted as far out as possible: a Store inside a loop of
//     trip count N becomes a single "+N" update outside the loop rather than
//     N "+1" updates inside it. This keeps the per-iteration overhead of
//     profiling close to zero.
//
//     If a counter update fails to hoist all the way out of a parallel loop
//     — typically because the contribution depends on the parallel loop var
//     itself, or on something defined inside the body — we don't want each
//     iteration to do an atomic add directly into the global stats struct.
//     Instead InjectCounters allocates a small thread-local buffer
//     ("local_counters") on the stack of each parallel task, accumulates
//     contributions there with ordinary (non-atomic) adds, and then emits
//     one halide_profiler_update_counters call per task at the end of the
//     loop body that folds the local buffer into the shared per-entry
//     counters with a single atomic update. This trades a fixed per-task
//     overhead for keeping the hot inner loop free of atomics.
//
//  3) InjectProfiling. Emits the rest of the runtime scaffolding:
//       - halide_profiler_set_current_func writes so the sampler knows which
//         instance the CPU is currently executing.
//       - halide_profiler_memory_allocate / _memory_free wrappers around
//         heap allocations, plus per-entry peak-stack accounting.
//       - halide_profiler_incr/decr_active_threads around parallel for, fork
//         and acquire, so the runtime can compute average active-thread
//         counts.
//       - Sampling-token plumbing so leaf parallel tasks contend cheaply for
//         the sampling slot.
//       - Copy-to-host / copy-to-device timing.
//
// After those three passes, inject_profiling wraps the whole IR with a
// halide_profiler_instance_start call (which registers the pipeline instance
// with the global profiler and returns an error code we assert on) and a
// register_destructor for halide_profiler_instance_end, and emits the
// func_names / func_parents tables the runtime needs to print the report.
//
// -----------------------------------------------------------------------------
// Inputs from earlier lowering passes
// -----------------------------------------------------------------------------
//
// ScheduleFunctions and Inline emit some intrinsics specifically for the
// profiler that this pass consumes and strips out:
//
//  - declare_box_required_at_realization(f, min0, max0, min1, max1, ...):
//    emitted by ScheduleFunctions at every Realize node. The box (mins/maxes
//    per dimension) is the set of points of f realized at that realize node.
//    We bill the box size to f's PointsRequiredAtRealization counter and
//    bump Realizations.
//
//  - declare_box_required_at_production(f, ...): emitted by ScheduleFunctions
//    just inside the ProducerConsumer node. Uses the same .s0.var.min/.max
//    vars, but bounds inference binds those to per-production values at this
//    scope. Captures over-computation that the realize-box counter misses
//    (e.g. sliding-window failure: many produce iterations each recomputing
//    the full realize box).
//
//  - declare_box_required_at_root(f, ...): emitted by ScheduleFunctions at the
//    outermost level for every Func. The box is the pipeline-wide total
//    number of points of f required by all consumers. We duplicate that
//    count into every entry for f as PointsRequiredAtRoot, so each entry
//    has the same shared denominator when computing a local recompute
//    ratio.
//
//  - declare_inlined(...): emitted by a post-pass after Inline.cpp, one per
//    inlined call chain inside a Provide. The args encode a small graph:
//    the first N handle args name nodes (node 0 is the surrounding
//    producer, nodes 1..N-1 are the inlined Funcs in the chain), and the
//    trailing int args are (parent_idx, callee_idx) edges. Each non-root
//    node represents one inlined call site and bills +1 (× surrounding
//    vector lanes) to InlinedCalls. PreAllocateEntries rewrites this
//    intrinsic in place; downstream we just read the resolved ids out of
//    the args and strip the intrinsic.
//
// -----------------------------------------------------------------------------
// Entries
// -----------------------------------------------------------------------------
//
// Each row in the per-Func stats array is an "entry". Most Funcs need
// exactly one entry, but some need several:
//
//  - An inlined Func called from multiple callers gets its body substituted
//    at every call site, and each substituted copy can be transformed
//    independently (different surrounding loop trip counts, different
//    vectorization, different inlining chains nested above it).
//
//  - A non-inlined Func whose update definitions have no explicit schedule
//    (e.g. h_1(x,y) += g_2(x+1,y); h_2(x,y) += g_2(x+1,y); with g_2 left at
//    its default schedule) gets a separate Realize/Producer block per
//    caller, because there's no shared compute_at level for it.
//
// Each such appearance gets its own entry and its own row in the report.
// Two reasons for tracking them separately rather than aggregating at
// billing time:
//
//  1) Each entry has a distinct parent Func — the producer or inlining
//     chain it lives inside. The reporter prints stats as a tree, so the
//     parent relationship is part of the output, not just a runtime detail.
//
//  2) Local stats (peak memory, recompute ratio, time%) are meaningful
//     per-entry. A Func called from a hot caller and a cold caller can
//     have very different profiles, and merging them would erase the
//     signal.
//
// Names::id_for_entry(name, parent_id) is the allocator: it gives back
// the same id for the same (name, parent) pair and a fresh id otherwise.
// PreAllocateEntries drives the allocation by walking ProducerConsumer
// nodes and declare_inlined intrinsics; the other passes look up the same
// ids by replaying the same producer-stack / inlining-chain context.
//
// EntryInfo::canonical_id remembers, for each entry, the first id that was
// allocated for its name. That lets the reporter optionally show a
// rolled-up "by Func" view, and lets per-Func warnings (heuristics like
// "this Func is being recomputed too much") fire once per Func rather than
// once per entry.

namespace {

// All names that need to be unique, just in case someone does something
// perverse like naming a func "profiler_instance".
struct Names {
    const std::string &pipeline_name;
    const std::map<std::string, Function> &env;
    std::string profiler_instance;
    std::string profiler_local_sampling_token;
    std::string profiler_shared_sampling_token;
    std::string hvx_profiler_instance;
    std::string profiler_func_names;
    std::string profiler_func_parents;
    std::string profiler_func_canonical_ids;
    std::string profiler_func_stack_peak_buf;
    std::string profiler_func_kinds;
    std::string profiler_func_flags;
    std::string profiler_func_buffer_func_ids;
    std::string profiler_start_error_code;

    // IDs 0-3 are reserved for bookkeeping slots; the kind field on each
    // disambiguates them from real Funcs at report time.
    const int overhead_id = 0, thread_idle_id = 1, malloc_id = 2, free_id = 3;

    Names(const std::string &pipeline_name, const std::map<std::string, Function> &env)
        : pipeline_name(pipeline_name),
          env(env),
          profiler_instance(unique_name("profiler_instance")),
          profiler_local_sampling_token(unique_name("profiler_local_sampling_token")),
          profiler_shared_sampling_token(unique_name("profiler_shared_sampling_token")),
          hvx_profiler_instance(unique_name("hvx_profiler_instance")),
          profiler_func_names(unique_name("profiler_func_names")),
          profiler_func_parents(unique_name("profiler_func_parents")),
          profiler_func_canonical_ids(unique_name("profiler_func_canonical_ids")),
          profiler_func_stack_peak_buf(unique_name("profiler_func_stack_peak_buf")),
          profiler_func_kinds(unique_name("profiler_func_kinds")),
          profiler_func_flags(unique_name("profiler_func_flags")),
          profiler_func_buffer_func_ids(unique_name("profiler_func_buffer_func_ids")),
          profiler_start_error_code(unique_name("profiler_start_error_code")) {

        // Reserve the bookkeeping slots first, so their indices match the
        // *_id constants. Each gets a specific kind so the reporter can
        // recognize it without hardcoding its index.
        struct {
            const char *name;
            halide_profiler_func_kind kind;
        } bookkeeping[] = {
            {"overhead", halide_profiler_func_kind_overhead},
            {"thread idle", halide_profiler_func_kind_thread_idle},
            {"malloc", halide_profiler_func_kind_malloc},
            {"free", halide_profiler_func_kind_free},
        };
        for (const auto &b : bookkeeping) {
            id_for_entry(b.name, -1, b.kind);
        }
    }

    // One EntryInfo per entry — see the file-level "Entries" comment.
    struct EntryInfo {
        std::string name;
        int parent_id;     // immediate parent entry, or -1 if at the root
        int canonical_id;  // first id allocated for this name, used for
                           // per-Func (rolled-up) reporting
        halide_profiler_func_kind kind;
        int buffer_func_id;  // for copy synthetics, canonical id of the
                             // Func whose buffer is being copied; -1
                             // otherwise
        // Initial value of halide_profiler_func_stats::flags. Read once
        // at instance-start time from the per-entry constants array;
        // the runtime does not subsequently mutate it.
        uint8_t flags;
    };
    std::vector<EntryInfo> entry_info;
    // First id allocated for each name — the canonical entry.
    std::map<std::string, int> canonical_id_for_name;
    // (parent_id, name) -> id — used to deduplicate when the same (parent,
    // name) pair is encountered more than once. Keyed on the IR-level
    // Function name so two appearances of the same Func dedup correctly,
    // even when the entry's display name is a Function::profiler_display_name
    // override.
    std::map<std::pair<int, std::string>, int> entry_map;

    // Get (or allocate) the id for a specific entry, keyed on the Func
    // name plus its immediate parent entry in the IR. Two appearances of
    // the same Func with different parents get different ids; two
    // appearances with the same parent share one id.
    int id_for_entry(const std::string &ir_name, int parent_id,
                     halide_profiler_func_kind kind = halide_profiler_func_kind_func,
                     int buffer_func_id = -1) {
        auto [it, inserted] = entry_map.try_emplace({parent_id, ir_name}, (int)entry_info.size());
        if (inserted) {
            int canon = canonical_id_for_name.try_emplace(ir_name, it->second).first->second;
            std::string display_name = ir_name;
            uint8_t flags = 0;
            if (kind == halide_profiler_func_kind_func) {
                auto eit = env.find(ir_name);
                if (eit != env.end()) {
                    const Function &f = eit->second;
                    if (!f.profiler_display_name().empty()) {
                        display_name = f.profiler_display_name();
                    }
                }
            }
            entry_info.push_back({display_name, parent_id, canon, kind, buffer_func_id, flags});
        }
        return it->second;
    }

    // Shorthand for the id of the root-level (parent = -1) entry for a
    // name. Used for Funcs we know have only one entry (e.g. produced at
    // the pipeline root).
    int id_for_name(const std::string &name) {
        return id_for_entry(name, -1);
    }

    std::string prefix(const std::string &name) const {
        size_t idx = name.find('.');
        if (idx != std::string::npos) {
            internal_assert(idx != 0) << name;
            return name.substr(0, idx);
        } else {
            return name;
        }
    }

    int num_ids() const {
        return (int)entry_info.size();
    }
};

// Unwrap a Broadcast(...) wrapper from an arg, then extract the Func name.
// declare_inlined / declare_box_required / declare_stage / inline_marker
// carry the Func name as a StringImm in their first arg — the name is just
// a label for the profiler report, not a symbol that exists in scope, so
// using a StringImm keeps it from being matched by passes like
// InjectHostDevBufferCopies that substitute Variables named after Funcs.
// The arg can show up wrapped in a Broadcast after vectorization.
const std::string &handle_name(const Expr &e) {
    const Expr *inner = &e;
    if (const Broadcast *b = e.as<Broadcast>()) {
        inner = &b->value;
    }
    const StringImm *s = inner->as<StringImm>();
    internal_assert(s) << e;
    return s->value;
}

// First pass: enumerate the entries (see file-level comment) and assign
// each one an id. Walks every ProducerConsumer node (one entry per
// producer, parented to the surrounding producer) and every declare_inlined
// intrinsic (one entry per non-root node of the inlining graph the
// intrinsic encodes). To save the next pass from re-parsing the
// declare_inlined graph, this pass also rewrites each declare_inlined to
// just the flat list of resolved entry ids — the intrinsic's type is
// preserved so the surrounding vector lane count is still available.
class PreAllocateEntries : public IRMutator {
    Names &names;
    const std::map<std::string, Function> &env;
    int producer_id = -1;

    // Entries minted by a Produce of a profiled Func. Used by visit
    // (Allocate) to decide whether the Allocate had a matching Produce
    // at the same scope — if not, the Allocate is a hoist_storage
    // allocation site and the entry is marked with
    // halide_profiler_func_kind_allocation.
    std::set<int> produce_minted_ids;

    bool is_profiled_func(const std::string &name) const {
        auto it = env.find(name);
        if (it == env.end()) {
            it = env.find(names.prefix(name));
        }
        return it != env.end() && !it->second.should_not_profile();
    }

    using IRMutator::visit;

    Stmt visit(const Allocate *op) override {
        // Eagerly mint the entry at the Allocate site so it falls in IR
        // order — for hoist_storage Funcs this puts the allocation
        // entry before the production entry in the report. For Funcs
        // without hoist_storage the Allocate and Produce sit in the
        // same scope and id_for_entry is idempotent.
        int id = -1;
        if (is_profiled_func(op->name)) {
            id = names.id_for_entry(op->name, producer_id);
        }
        Stmt result = IRMutator::visit(op);
        // After walking the body, the matching Produce (if any at this
        // scope) has been minted. If not, this is a hoist_storage
        // allocation site — mark its kind accordingly.
        if (id >= 0 && !produce_minted_ids.count(id)) {
            names.entry_info[id].kind = halide_profiler_func_kind_allocation;
        }
        return result;
    }

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            int id = names.id_for_entry(op->name, producer_id);
            produce_minted_ids.insert(id);
            ScopedValue<int> old(producer_id, id);
            return IRMutator::visit(op);
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        // Eagerly allocate ids for the synthetic copy "Funcs" produced by
        // InjectProfiling::inject_buffer_copy_timing. Allocating them here
        // (during the encounter-order walk) rather than later in
        // InjectProfiling makes the ids fall in IR order alongside their
        // sibling producers, so the report's DFS-by-id traversal renders
        // them in their correct timeline position. id_for_entry is
        // idempotent on (name, parent), so InjectProfiling's later lookup
        // returns the same id. Parent is the immediately enclosing
        // producer, so a copy_to_device(foo) emitted inside Produce(bar)
        // nests under bar in the report — making it clear which consumer
        // the copy was preparing data for.
        if (op->name == "halide_copy_to_host" || op->name == "halide_copy_to_device") {
            if (!op->args.empty()) {
                if (const Variable *v = op->args.front().as<Variable>()) {
                    if (ends_with(v->name, ".buffer")) {
                        std::string buffer_name = v->name.substr(0, v->name.size() - 7);
                        bool to_device = op->name == "halide_copy_to_device";
                        const char *tag = to_device ? " (copy to device)" : " (copy to host)";
                        halide_profiler_func_kind kind =
                            to_device ? halide_profiler_func_kind_copy_to_device : halide_profiler_func_kind_copy_to_host;
                        // Look up the canonical id of the Func whose
                        // buffer this is. The Func's producer has
                        // already been visited (it's the producer this
                        // copy sits inside, or a sibling allocated
                        // earlier), so the name is in the map.
                        int buffer_func_id = -1;
                        auto it = names.canonical_id_for_name.find(buffer_name);
                        if (it != names.canonical_id_for_name.end()) {
                            buffer_func_id = it->second;
                        }
                        names.id_for_entry(buffer_name + tag, producer_id, kind, buffer_func_id);
                    }
                }
            }
            return IRMutator::visit(op);
        }
        if (!op->is_intrinsic(Call::declare_inlined)) {
            return IRMutator::visit(op);
        }
        // Parse the (node_0, ..., node_{N-1}, p_0, c_0, ...) form.
        int num_nodes = 0;
        while (num_nodes < (int)op->args.size() &&
               op->args[num_nodes].type().is_handle()) {
            num_nodes++;
        }
        int num_edges = ((int)op->args.size() - num_nodes) / 2;
        internal_assert(num_nodes >= 1 &&
                        (int)op->args.size() == num_nodes + 2 * num_edges);

        // Collect all incoming edges per node. A node can have multiple
        // parents when CSE within a Provide shares a marker subexpression
        // between two different inlining contexts (the diamond case).
        std::vector<std::vector<int>> parents(num_nodes);
        for (int e = 0; e < num_edges; e++) {
            auto p = as_const_int(op->args[num_nodes + 2 * e]);
            auto c = as_const_int(op->args[num_nodes + 2 * e + 1]);
            internal_assert(p && c) << Expr(op);
            parents[*c].push_back((int)*p);
        }

        // For each node, the sorted-by-index list of all its ancestors
        // (including itself), and its depth from the root. Computed via
        // recursive memoization so dependencies are resolved in topological
        // order regardless of how the node indices compare — node indices
        // reflect walk-encounter order, not topology (e.g. a let-bound
        // marker is added before the markers that reference it).
        std::vector<std::vector<int>> ancestors(num_nodes);
        std::vector<int> depth(num_nodes, 0);
        std::vector<bool> done(num_nodes, false);
        auto compute_ancestors = [&](auto &self, int n) -> void {
            if (done[n]) {
                return;
            }
            done[n] = true;
            ancestors[n] = {n};
            for (int p : parents[n]) {
                self(self, p);
                depth[n] = std::max(depth[n], depth[p] + 1);
                std::vector<int> merged;
                std::set_union(ancestors[n].begin(), ancestors[n].end(),
                               ancestors[p].begin(), ancestors[p].end(),
                               std::back_inserter(merged));
                ancestors[n] = std::move(merged);
            }
        };
        for (int n = 0; n < num_nodes; n++) {
            compute_ancestors(compute_ancestors, n);
        }

        // For each non-root node pick its "effective" parent. Single parent:
        // just that parent. Multiple parents (the diamond case): the lowest
        // common ancestor — the deepest node every parent agrees on.
        std::vector<int> node_parent_idx(num_nodes, -1);
        for (int n = 1; n < num_nodes; n++) {
            internal_assert(!parents[n].empty())
                << "Non-root node " << n << " has no parent in declare_inlined";
            if (parents[n].size() == 1) {
                node_parent_idx[n] = parents[n][0];
                continue;
            }
            std::vector<int> common = ancestors[parents[n][0]];
            std::vector<int> tmp;
            for (size_t i = 1; i < parents[n].size(); i++) {
                tmp.clear();
                std::set_intersection(common.begin(), common.end(),
                                      ancestors[parents[n][i]].begin(),
                                      ancestors[parents[n][i]].end(),
                                      std::back_inserter(tmp));
                std::swap(common, tmp);
            }
            // common always contains at least the root (which is every
            // node's ancestor). Pick the deepest.
            int best = common[0];
            for (int a : common) {
                if (depth[a] > depth[best]) {
                    best = a;
                }
            }
            node_parent_idx[n] = best;
        }

        // Node 0 is the surrounding producer; the rest chain back to it.
        std::vector<int> node_id(num_nodes, -1);
        node_id[0] = producer_id;
        auto resolve_entry_id = [&](auto &self, int idx) -> int {
            if (node_id[idx] >= 0) {
                return node_id[idx];
            }
            int pid = self(self, node_parent_idx[idx]);
            const std::string &name = handle_name(op->args[idx]);
            node_id[idx] = names.id_for_entry(name, pid);
            return node_id[idx];
        };
        for (int i = 1; i < num_nodes; i++) {
            resolve_entry_id(resolve_entry_id, i);
        }

        std::vector<Expr> new_args;
        new_args.reserve(num_nodes - 1);
        for (int i = 1; i < num_nodes; i++) {
            new_args.push_back(make_const(Int(32), node_id[i]));
        }
        return Call::make(op->type, Call::declare_inlined,
                          new_args, Call::Intrinsic);
    }

public:
    PreAllocateEntries(Names &names, const std::map<std::string, Function> &env)
        : names(names), env(env) {
    }
};

// Second pass: inject counters for various stats as far outermost as possible,
// to minimize overhead. For example, instead of this:
// for (x in [min, max]) {
//   increment_counter(..., 1);
//   ...
// }
// We want to inject this:
// increment_counter(..., max - min + 1);
// for (x in [min, max]) {
//   ...
// }
class InjectCounters : public IRMutator {
public:
    InjectCounters(Names &names, const map<string, Function> &env)
        : names(names), env(env) {
        // The previous pass populated names.entry_info with every entry.
        // Index them by name so declare_box_required_root (which carries a
        // pipeline-wide root-box count for a Func, not for any specific
        // entry for it) can duplicate that count to every entry.
        for (int i = 0; i < names.num_ids(); i++) {
            entries_by_name[names.entry_info[i].name].push_back(i);
        }
    }

    // Entries whose counter contributions were hoisted out of a GPU
    // kernel via upper-bound substitution (or summed across an impure-
    // condition IfThenElse) and so may be over-counted in the report.
    std::set<int> approximated_entries;

protected:
    Names &names;
    const map<string, Function> &env;

    using IRMutator::visit;

    // ID of the currently-produced Func
    int producer_id = -1;

    // Per-Func flag: are we currently inside that Func's pure def?
    // Updated by the declare_stage marker that ScheduleFunctions emits
    // at the start of each stage's loop nest (the marker also carries
    // the Func name). A per-Func map handles compute_with cases where
    // the stages of different Funcs are interleaved in the IR — each
    // Store consults the flag for its own Func.
    std::map<std::string, bool> func_in_pure_stage;

    // The counters we track. This list must be kept in sync with multiple other
    // things. If you add a counter, also update:
    // - the num_counters int below the enum
    // - halide_profile_update_counters in profiler_inlined.cpp
    // - the fields of halide_profiler_func_stats in HalideRuntime.h
    // - the block of code that prints counters to json in profiler_common.cpp
    enum { Realizations = 0,
           Productions,
           ParallelLoops,
           ParallelTasks,
           PointsRequiredAtRealization,
           PointsRequiredAtProduction,
           PointsRequiredAtRoot,
           PointsRequiredInwards,
           ProductionsIfInwards,
           PointsComputed,
           ScalarLoads,
           VectorLoads,
           Gathers,
           BytesLoaded,
           ScalarStores,
           VectorStores,
           Scatters,
           BytesStored,
           InlinedCalls };

    static constexpr int num_counters = InlinedCalls + 1;

    struct Counters {

        Expr counters[num_counters];

        void add(const Counters &other) {
            for (int i = 0; i < num_counters; i++) {
                if (counters[i].defined()) {
                    if (other.counters[i].defined()) {
                        counters[i] += other.counters[i];
                    }
                } else {
                    counters[i] = other.counters[i];
                }
            }
            free_vars.insert(other.free_vars.begin(), other.free_vars.end());
        }

        void mul(const Expr &e) {
            for (int i = 0; i < num_counters; i++) {
                if (counters[i].defined()) {
                    counters[i] *= e;
                }
            }
            add_free_vars(e);
        }

        void count(int c, const Expr &e) {
            internal_assert(e.defined() && e.type() == UInt(64)) << e;
            if (counters[c].defined()) {
                counters[c] += e;
            } else {
                counters[c] = e;
            }
            add_free_vars(e);
        }

        void count(int c) {
            count(c, make_one(UInt(64)));
        }

        void add_free_vars(const Expr &e) {
            visit_with(e, [&](auto *, const Variable *var) {
                free_vars.insert(var->name);
            });
        }

        // The free vars in the expressions
        std::set<std::string> free_vars;
    };

    const For *enclosing_loop = nullptr, *enclosing_parallel_loop = nullptr;
    // True while mutating the body of any GPU loop. The CPU local_counters
    // mechanism doesn't translate to GPU code (the buffer would have to be
    // device-accessible, with atomic adds, and IHDBC has already run by the
    // time we're injecting profiling). Instead, when in_gpu is set, we
    // make any counter contribution that would normally have to flush
    // mid-kernel hoist conservatively out of the kernel — substituting an
    // upper bound for loop vars, wrapping in a Let for LetStmts, and
    // wrapping in a Select for IfThenElse (or taking the max of the
    // branches when the condition is impure). Any entry whose contribution
    // is hoisted via an upper-bound substitution is recorded in
    // approximated_entries and flagged in its profile-report notes.
    bool in_gpu = false;
    // thread-local counters
    std::string local_counters;
    // A map from a func id and a counter id to a slot in the local counters array
    std::map<std::pair<int, int>, int> local_counters_indices;

    std::map<int, Counters> counters;

    // name -> all entry ids with that name. Built once in the constructor;
    // only declare_box_required_root reads it.
    std::map<std::string, std::vector<int>> entries_by_name;

    bool is_func(const std::string &name) const {
        return env.find(name) != env.end();
    }

    Stmt flush(const Stmt &s, int id, const Counters &c) {
        if (enclosing_loop &&
            enclosing_parallel_loop &&
            enclosing_loop != enclosing_parallel_loop) {
            // Flush to local counters
            if (local_counters.empty()) {
                local_counters = unique_name("local_counters");
            }
            std::vector<Stmt> stores;
            stores.reserve(num_counters);
            // TODO: Joint CSE across RHSs
            for (int i = 0; i < num_counters; i++) {
                if (!c.counters[i].defined()) {
                    continue;
                }
                int n = (int)local_counters_indices.size();
                int idx =
                    local_counters_indices.try_emplace({id, i}, n).first->second;
                Expr old = Load::make(UInt(64), local_counters, idx,
                                      Buffer<>{}, Parameter{}, const_true(), ModulusRemainder{});
                stores.push_back(Store::make(local_counters, old + c.counters[i], idx,
                                             Parameter{}, const_true(), ModulusRemainder{}));
            }
            stores.push_back(s);
            return Block::make(stores);
        } else {
            // Flush to global counters
            std::vector<Expr> args(2 + num_counters);
            args[0] = Variable::make(Handle(), names.profiler_instance);
            args[1] = id;
            // TODO: Joint CSE across RHSs
            for (int i = 0; i < num_counters; i++) {
                Expr count = c.counters[i];
                args[i + 2] = count.defined() ? count : make_zero(UInt(64));
            }
            Expr call = Call::make(Int(32), "halide_profiler_update_counters", args, Call::Extern);
            return Block::make(Evaluate::make(std::move(call)), s);
        }
    }

    Stmt flush_all(const Stmt &stmt) {
        Stmt s = stmt;
        for (auto p : counters) {
            s = flush(s, p.first, p.second);
        }
        counters.clear();
        return s;
    }

    Stmt flush_all_that_depend_on_var(const Stmt &stmt, const std::string &var) {
        Stmt s = stmt;
        for (auto it = counters.begin(); it != counters.end();) {
            const auto &[id, c] = *it;
            if (c.free_vars.count(var)) {
                s = flush(s, id, c);
                it = counters.erase(it);
            } else {
                it++;
            }
        }
        return s;
    }

    void merge(const std::map<int, Counters> &other) {
        for (const auto &it : other) {
            counters[it.first].add(it.second);
        }
    }

    // Recompute a Counters object's free_vars set from its current Exprs.
    // Used after any hoisting operation that mutates the Exprs in place.
    static void recompute_free_vars(Counters &c) {
        c.free_vars.clear();
        for (int i = 0; i < num_counters; i++) {
            if (c.counters[i].defined()) {
                c.add_free_vars(c.counters[i]);
            }
        }
    }

    // GPU hoisting: if `var` (a closing-out loop var) appears in a counter
    // contribution, substitute an upper bound for it so the contribution
    // becomes hoistable. Mark the entry as approximated since the result
    // is an over-estimate. If no finite upper bound can be found, drop the
    // contribution entirely (still mark approximated).
    void hoist_loop_var_upper_bound(const For *op) {
        Scope<Interval> scope;
        scope.push(op->name, Interval(op->min, op->max));
        for (auto &[id, c] : counters) {
            if (!c.free_vars.count(op->name)) {
                continue;
            }
            for (int i = 0; i < num_counters; i++) {
                if (c.counters[i].defined() && expr_uses_var(c.counters[i], op->name)) {
                    Interval iv = bounds_of_expr_in_scope(c.counters[i], scope);
                    if (iv.has_upper_bound()) {
                        c.counters[i] = simplify(iv.max);
                    } else {
                        c.counters[i] = Expr();
                    }
                    approximated_entries.insert(id);
                }
            }
            recompute_free_vars(c);
        }
    }

    // GPU hoisting: when a LetStmt is closing out, any counter contribution
    // that depends on the let-bound name gets wrapped in an exact Let —
    // but only if the RHS is pure. An impure RHS (e.g. a Load whose
    // backing buffer may be mutated, or a non-pure Call) would be
    // re-evaluated in a different scope by the wrapped Let, which can
    // change its meaning. In that case we drop the contribution and mark
    // the entry approximated.
    void hoist_let(const std::string &name, const Expr &value) {
        bool value_pure = is_pure(value);
        for (auto &[id, c] : counters) {
            if (!c.free_vars.count(name)) {
                continue;
            }
            for (int i = 0; i < num_counters; i++) {
                if (c.counters[i].defined() && expr_uses_var(c.counters[i], name)) {
                    if (value_pure) {
                        c.counters[i] = Let::make(name, value, c.counters[i]);
                    } else {
                        c.counters[i] = Expr();
                        approximated_entries.insert(id);
                    }
                }
            }
            recompute_free_vars(c);
        }
    }

    // GPU hoisting: combine the then- and else-branch counter contributions
    // of an IfThenElse into the outer scope. For a pure condition, exact via
    // Select. For an impure condition (e.g. a Load), upper-bound the
    // contribution by max(then, else) (the branches are mutually exclusive)
    // and mark the entries as approximated.
    void hoist_if(const Expr &condition,
                  std::map<int, Counters> &then_counters,
                  std::map<int, Counters> &else_counters) {
        bool cond_pure = is_pure(condition);
        std::set<int> ids;
        for (const auto &p : then_counters) {
            ids.insert(p.first);
        }
        for (const auto &p : else_counters) {
            ids.insert(p.first);
        }
        for (int id : ids) {
            Counters merged;
            auto *t = then_counters.count(id) ? &then_counters[id] : nullptr;
            auto *e = else_counters.count(id) ? &else_counters[id] : nullptr;
            for (int i = 0; i < num_counters; i++) {
                Expr tv = (t && t->counters[i].defined()) ? t->counters[i] : Expr();
                Expr ev = (e && e->counters[i].defined()) ? e->counters[i] : Expr();
                if (!tv.defined() && !ev.defined()) {
                    continue;
                }
                Expr zero64 = make_zero(UInt(64));
                Expr ti = tv.defined() ? tv : zero64;
                Expr ei = ev.defined() ? ev : zero64;
                if (cond_pure) {
                    merged.counters[i] = select(condition, ti, ei);
                } else {
                    // Branches are mutually exclusive — only one runs per
                    // execution — so the tight conservative upper bound on
                    // the contribution is max(then, else).
                    merged.counters[i] = max(ti, ei);
                    approximated_entries.insert(id);
                }
            }
            recompute_free_vars(merged);
            counters[id].add(merged);
        }
    }

    // Compute the total number of points in a box passed to declare_box_required*.
    // The args after the func handle are (min, max) pairs per dim; the result is
    // a scalar UInt(64) total, reduced across any surrounding vector lanes.
    static Expr box_total(const Call *op) {
        int lanes = op->type.lanes();
        Expr total = make_one(UInt(64, lanes));
        for (size_t i = 1; i < op->args.size(); i += 2) {
            total *= cast(total.type(), op->args[i + 1] + 1 - op->args[i]);
        }
        if (lanes > 1) {
            total = VectorReduce::make(VectorReduce::Add, total, 1);
        }
        // Simplifying here removes false dependences on loop vars.
        return simplify(total);
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::declare_box_required_at_realization)) {
            // Emitted at the Realize node for the func. The Realize sits
            // inside the parent producer and contains this func's
            // ProducerConsumer, so the right entry id is parented to the
            // current producer.
            Counters &c = counters[names.id_for_entry(handle_name(op->args[0]), producer_id)];
            c.count(PointsRequiredAtRealization, box_total(op));
            c.count(Realizations);
            return make_zero(op->type);
        } else if (op->is_intrinsic(Call::declare_box_required_at_production)) {
            // Emitted just inside the ProducerConsumer node. The same
            // .s0.var.min/.max vars used at the Realize site are also bound
            // here by bounds inference, but to the per-production values,
            // so this captures e.g. sliding-window over-computation that
            // the realize-box counter misses.
            Counters &c = counters[names.id_for_entry(handle_name(op->args[0]), producer_id)];
            c.count(PointsRequiredAtProduction, box_total(op));
            return make_zero(op->type);
        } else if (op->is_intrinsic(Call::declare_box_required_at_root)) {
            // Bill the pipeline-wide root box to this Func's canonical
            // entry only. It's a Func-level fact, not a per-entry one, so
            // summing it across entries would over-count. The reporter
            // looks it up via fs->canonical_id when computing each entry's
            // local recompute ratio.
            auto it = entries_by_name.find(handle_name(op->args[0]));
            if (it != entries_by_name.end()) {
                // entries_by_name was filled in id-ascending order, and the
                // canonical id is the first id allocated for the name, so
                // it->second.front() is always the canonical id.
                counters[it->second.front()].count(PointsRequiredAtRoot, box_total(op));
            }
            return make_zero(op->type);
        } else if (op->is_intrinsic(Call::declare_box_required_inwards)) {
            // Emitted one compute_at level further in than the current compute_at.
            auto it = entries_by_name.find(handle_name(op->args[0]));
            if (it != entries_by_name.end()) {
                counters[it->second.front()].count(PointsRequiredInwards, box_total(op));
                counters[it->second.front()].count(ProductionsIfInwards);
            }
            return make_zero(op->type);
        } else if (op->is_intrinsic(Call::declare_inlined)) {
            // The pre-pass has already resolved each non-root chain node to
            // a per-entry id, so the args here are just a flat list of int
            // ids — one per inlined call site to bill. The intrinsic's type
            // still carries the surrounding vector lane count.
            Expr per_node = make_const(UInt(64), op->type.lanes());

            for (const Expr &arg : op->args) {
                auto id = as_const_int(arg);
                internal_assert(id);
                counters[(int)*id].count(InlinedCalls, per_node);
            }

            return make_zero(op->type);
        } else if (op->is_intrinsic(Call::declare_stage)) {
            // Marker from ScheduleFunctions saying "we're starting stage N
            // of Func F here". Update our per-Func pure-def flag and strip
            // the marker from the IR.
            internal_assert(op->args.size() == 2);
            auto stage = as_const_int(op->args[1]);
            internal_assert(stage);
            func_in_pure_stage[handle_name(op->args[0])] = (*stage == 0);
            return make_zero(op->type);
        } else {
            // Counter events are never nested, so no recursive mutate call.
            return op;
        }
    }

    // True for Stores/Loads to or from buffers we want to bill: a Func's
    // own storage, or an input/output parameter / image. False for internal
    // bookkeeping buffers like storage-folding head trackers, async
    // semaphores, and sampling tokens — we don't want their accesses
    // inflating the byte / scalar-load counts of the enclosing producer.
    bool is_real_data_buffer(const Store *op) const {
        return op->param.defined() || is_func(names.prefix(op->name));
    }
    bool is_real_data_buffer(const Load *op) const {
        return op->param.defined() || op->image.defined() ||
               is_func(names.prefix(op->name));
    }

    Stmt visit(const Store *op) override {
        if (is_real_data_buffer(op)) {
            std::string f = names.prefix(op->name);
            // Stores in a producer block are to the Func being produced, so
            // bill them to the current producer's entry id. (That's the
            // right entry even if f has multiple entries elsewhere.)
            int id = (producer_id >= 0 && names.entry_info[producer_id].name == f) ?
                         producer_id :
                         names.id_for_name(f);
            Counters &c = counters[id];
            int lanes = op->value.type().lanes();
            if (op->index.type().is_scalar()) {
                c.count(ScalarStores);
            } else if (const Ramp *r = op->index.as<Ramp>();
                       r && is_const_one(r->stride)) {
                c.count(VectorStores);
            } else {
                c.count(Scatters);
            }
            c.count(BytesStored, make_const(UInt(64), op->value.type().bytes() * lanes));
            // Only the pure def (stage 0) contributes to "points computed";
            // update-def stores are a separate kind of work and shouldn't
            // show up as recompute. For Tuple-valued Funcs each output
            // point produces one Store per tuple element (to buffers
            // f.0, f.1, ...), so counting all of them would inflate
            // points_computed by the tuple arity. Skip any store whose
            // buffer name's final dotted component parses as a non-zero
            // integer -- those are the non-canonical tuple elements.
            auto it = func_in_pure_stage.find(f);
            if (it != func_in_pure_stage.end() && it->second) {
                size_t last_dot = op->name.rfind('.');
                const char *tail = (last_dot == std::string::npos) ? op->name.c_str() : op->name.c_str() + last_dot + 1;
                char *end = nullptr;
                long idx = std::strtol(tail, &end, 10);
                bool is_dup_tuple_element = (*end == '\0' && idx != 0);
                if (!is_dup_tuple_element) {
                    c.count(PointsComputed, make_const(UInt(64), lanes));
                }
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Load *op) override {
        // We bill these to the Func we're producing, not the Func being
        // loaded. These counters are about what kinds of loads we do while
        // computing a given Func.
        if (producer_id >= 0 && is_real_data_buffer(op)) {
            Counters &c = counters[producer_id];
            if (op->index.type().is_scalar()) {
                c.count(ScalarLoads);
            } else if (const Ramp *r = op->index.as<Ramp>();
                       r && is_const_one(r->stride)) {
                c.count(VectorLoads);
            } else {
                c.count(Gathers);
            }
            c.count(BytesLoaded, make_const(UInt(64), op->type.bytes() * op->type.lanes()));
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            // One entry per producer node, parented to the surrounding
            // producer. See file-level comment for why this matters.
            int id = names.id_for_entry(op->name, producer_id);
            Counters &c = counters[id];
            c.count(Productions);
            ScopedValue<int> old(producer_id, id);
            return IRMutator::visit(op);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const For *op) override {
        // GPU loops are also is_unordered_parallel(). We can't use the CPU
        // local_counters mechanism inside a GPU kernel (device memory,
        // atomics, IHDBC ordering), so when in_gpu we hoist any
        // closing-out contributions instead of flushing — see the helper
        // methods above.
        ScopedValue<bool> bind_gpu(in_gpu, in_gpu || is_gpu(op->for_type));

        decltype(counters) old;
        old.swap(counters);

        Stmt body;
        {
            ScopedValue<const For *> bind1(enclosing_loop, op);
            ScopedValue<const For *> bind2(enclosing_parallel_loop,
                                           op->is_unordered_parallel() ?
                                               op :
                                               enclosing_parallel_loop);
            body = mutate(op->body);
        }

        if (op->is_unordered_parallel() &&
            !local_counters.empty()) {
            // Flush any thread-local counters to global state
            std::map<int, Counters> to_flush;
            for (auto [p, idx] : local_counters_indices) {
                auto [id, counter] = p;
                to_flush[id].counters[counter] =
                    Load::make(UInt(64), local_counters, idx,
                               Buffer<>{}, Parameter{}, const_true(), ModulusRemainder{});
            }

            counters.swap(to_flush);
            Stmt post_flush = flush_all(Evaluate::make(0));
            counters.swap(to_flush);

            std::vector<Stmt> stmts;
            stmts.reserve(local_counters_indices.size() + 2);
            for (int i = 0; i < (int)local_counters_indices.size(); i++) {
                stmts.push_back(Store::make(local_counters, make_zero(UInt(64)), i,
                                            Parameter{}, const_true(), ModulusRemainder{}));
            }

            stmts.push_back(std::move(body));
            stmts.push_back(post_flush);
            body = Block::make(stmts);

            Expr size = (int)local_counters_indices.size();
            body = Allocate::make(local_counters, UInt(64), MemoryType::Stack, {size}, const_true(), body);
        }

        // Scale up the counters by the loop trip count
        Expr e = simplify(op->extent());

        if (in_gpu) {
            // Don't try to flush in the middle of a GPU kernel — hoist
            // depending contributions out via upper-bound substitution.
            hoist_loop_var_upper_bound(op);
        } else {
            body = flush_all_that_depend_on_var(body, op->name);
        }

        for (auto &[_, c] : counters) {
            c.mul(e);
        }

        merge(old);

        if (op->is_unordered_parallel()) {
            // The parallel loop belongs to the currently-producing Func.
            int id = producer_id >= 0 ? producer_id : names.id_for_name(names.prefix(op->name));
            counters[id].count(ParallelLoops);
            counters[id].count(ParallelTasks, cast(UInt(64), e));
        }

        return For::make(op->name, op->min, op->max,
                         op->for_type, op->partition_policy, op->device_api, std::move(body));
    }

    Stmt visit(const LetStmt *op) override {
        decltype(counters) old;
        counters.swap(old);
        Stmt body = mutate(op->body);
        if (in_gpu) {
            hoist_let(op->name, op->value);
        } else {
            body = flush_all_that_depend_on_var(body, op->name);
        }
        merge(old);
        return LetStmt::make(op->name, op->value, body);
    }

    Stmt visit(const IfThenElse *op) override {
        if (in_gpu) {
            // Inside a GPU kernel we can't flush in the branches; instead
            // combine the branch contributions into the outer scope via
            // Select (or a conservative max of the branches if the
            // condition is impure).
            decltype(counters) outer;
            counters.swap(outer);
            Stmt then_case = mutate(op->then_case);
            decltype(counters) then_counters;
            counters.swap(then_counters);
            Stmt else_case;
            decltype(counters) else_counters;
            if (op->else_case.defined()) {
                else_case = mutate(op->else_case);
                counters.swap(else_counters);
            }
            counters.swap(outer);
            hoist_if(op->condition, then_counters, else_counters);
            return IfThenElse::make(op->condition, then_case, else_case);
        }

        decltype(counters) old;
        counters.swap(old);
        Stmt then_case, else_case;
        then_case = mutate(op->then_case);
        then_case = flush_all(then_case);
        if (op->else_case.defined()) {
            else_case = mutate(op->else_case);
            else_case = flush_all(else_case);
        }
        counters.swap(old);
        return IfThenElse::make(op->condition, then_case, else_case);
    }

    Stmt visit(const Block *op) override {
        // Put the outermost counter update just outside the timing start
        const Evaluate *eval = op->first.as<Evaluate>();
        const Call *call = eval ? eval->value.as<Call>() : nullptr;
        if (call && call->is_intrinsic(Call::profiling_enable_instance_marker)) {
            return flush_all(IRMutator::visit(op));
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    Stmt operator()(const Stmt &s) {
        return flush_all(IRMutator::operator()(s));
    }
};

class InjectProfiling : public IRMutator {
    // Thread-activity tracking around parallel constructs and sampling-token
    // plumbing for leaf parallel tasks.
    static Stmt incr_active_threads(const Expr &profiler_instance) {
        return Evaluate::make(Call::make(Int(32), "halide_profiler_incr_active_threads",
                                         {profiler_instance}, Call::Extern));
    }

    static Stmt decr_active_threads(const Expr &profiler_instance) {
        return Evaluate::make(Call::make(Int(32), "halide_profiler_decr_active_threads",
                                         {profiler_instance}, Call::Extern));
    }

    static Stmt acquire_sampling_token(const Expr &shared_token, const Expr &local_token) {
        return Evaluate::make(Call::make(Int(32), "halide_profiler_acquire_sampling_token",
                                         {shared_token, local_token}, Call::Extern));
    }

    static Stmt release_sampling_token(const Expr &shared_token, const Expr &local_token) {
        return Evaluate::make(Call::make(Int(32), "halide_profiler_release_sampling_token",
                                         {shared_token, local_token}, Call::Extern));
    }

    static Stmt claim_sampling_token(const Stmt &s, const Expr &shared_token, const Expr &local_token) {
        return LetStmt::make(local_token.as<Variable>()->name,
                             Call::make(Handle(), Call::alloca, {Int(32).bytes()}, Call::Intrinsic),
                             Block::make({acquire_sampling_token(shared_token, local_token),
                                          s,
                                          release_sampling_token(shared_token, local_token)}));
    }

public:
    vector<int> stack;  // What produce nodes are we currently inside of.

    Names &names;
    const map<string, Function> &env;

    bool in_fork = false;
    bool in_parallel = false;
    bool in_leaf_task = false;

    InjectProfiling(Names &names, const map<std::string, Function> &env)
        : names(names), env(env) {

        stack.push_back(names.overhead_id);

        profiler_instance = Variable::make(Handle(), names.profiler_instance);
        profiler_local_sampling_token = Variable::make(Handle(), names.profiler_local_sampling_token);
        profiler_shared_sampling_token = Variable::make(Handle(), names.profiler_shared_sampling_token);
    }

    map<int, uint64_t> func_stack_current;  // map from func id -> current stack allocation
    map<int, uint64_t> func_stack_peak;     // map from func id -> peak stack allocation

    Stmt activate_thread(const Stmt &s) {
        return activate_thread_helper(s, names.thread_idle_id);
    }

    Stmt activate_main_thread(const Stmt &s) {
        // The same as a child task, except when we finish (but before the
        // instances get popped), bill anything as overhead.
        return activate_thread_helper(s, 0);
    }

    Stmt activate_thread_helper(const Stmt &s, int final_id) {
        return Block::make({incr_active_threads(profiler_instance),
                            unconditionally_set_current_func(stack.back()),
                            s,
                            decr_active_threads(profiler_instance),
                            unconditionally_set_current_func(final_id)});
    }

    Stmt suspend_thread(const Stmt &s) {
        return Block::make({decr_active_threads(profiler_instance),
                            unconditionally_set_current_func(names.thread_idle_id),
                            s,
                            incr_active_threads(profiler_instance),
                            unconditionally_set_current_func(stack.back())});
    }

    Stmt suspend_thread_but_keep_task_id(const Stmt &s) {
        return Block::make({decr_active_threads(profiler_instance),
                            s,
                            incr_active_threads(profiler_instance)});
    }

private:
    using IRMutator::visit;

    Expr profiler_instance;
    Expr profiler_local_sampling_token;
    Expr profiler_shared_sampling_token;

    // May need to be set to -1 at the start of control flow blocks
    // that have multiple incoming edges, if all sources don't have
    // the same most_recently_set_func.
    int most_recently_set_func = -1;

    struct AllocSize {
        bool on_stack;
        Expr size;
        // Entry id resolved at Allocate time. The matching Free may sit at
        // a different point in the producer stack than the Allocate (the
        // Allocate can be hoisted while the Free stays deep), so caching the
        // id here keeps the two billed to the same entry.
        int id;
    };

    Scope<AllocSize> func_alloc_sizes;

    bool profiling_memory = true;

    enum class Kind { ProfiledFunc = 0,
                      NonProfiledFunc,
                      NotAFunc };

    Kind classify(const std::string &name) const {
        auto it = env.find(name);
        if (it == env.end()) {
            it = env.find(names.prefix(name));
        }
        if (it != env.end()) {
            if (it->second.should_not_profile()) {
                return Kind::NonProfiledFunc;
            } else {
                return Kind::ProfiledFunc;
            }
        } else {
            return Kind::NotAFunc;
        }
    }

    // Resolve a Func name to its entry id under the currently-active
    // producer chain. Must match the entry id PreAllocateEntries
    // allocated for the corresponding producer.
    int get_func_entry_id(const string &name) {
        int parent = stack.back();
        // The bottom of the stack is the overhead sentinel; treat it as "no
        // parent" so pipeline-root producers come out as parent=-1, matching
        // how PreAllocateEntries allocates them (its producer_id starts at -1).
        if (parent == names.overhead_id) {
            parent = -1;
        }
        return names.id_for_entry(names.prefix(name), parent);
    }

    Stmt unconditionally_set_current_func(int id) {
        Stmt s = Evaluate::make(Call::make(Int(32), "halide_profiler_set_current_func",
                                           {profiler_instance, id, reinterpret(Handle(), make_zero(UInt(64)))}, Call::Extern));
        return s;
    }

    Stmt set_current_func(int id) {
        if (most_recently_set_func == id) {
            return Evaluate::make(0);
        }
        most_recently_set_func = id;
        Expr last_arg = in_leaf_task ? profiler_local_sampling_token : reinterpret(Handle(), make_zero(UInt(64)));
        // This call gets inlined and becomes a single store instruction.
        Stmt s = Evaluate::make(Call::make(Int(32), "halide_profiler_set_current_func",
                                           {profiler_instance, id, last_arg}, Call::Extern));

        return s;
    }

    Expr compute_allocation_size(const vector<Expr> &extents,
                                 const Expr &condition,
                                 const Type &type,
                                 const std::string &name,
                                 bool &can_fit_on_stack) {
        can_fit_on_stack = true;

        Expr cond = simplify(condition);
        if (is_const_zero(cond)) {  // Condition always false
            return make_zero(UInt(64));
        }

        int64_t constant_size = Allocate::constant_allocation_size(extents, name);
        if (constant_size > 0) {
            int64_t stack_bytes = constant_size * type.bytes();
            if (can_allocation_fit_on_stack(stack_bytes)) {  // Allocation on stack
                return make_const(UInt(64), stack_bytes);
            }
        }

        // Check that the allocation is not scalar (if it were scalar
        // it would have constant size).
        internal_assert(!extents.empty());

        can_fit_on_stack = false;
        Expr size = cast<uint64_t>(extents[0]);
        for (size_t i = 1; i < extents.size(); i++) {
            size *= extents[i];
        }
        size = simplify(Select::make(condition, size * type.bytes(), make_zero(UInt(64))));
        return size;
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::profiling_enable_instance_marker)) {
            // We're out of the bounds query code. This instance should be
            // tracked (including any samples taken before this point.
            return Call::make(Int(32), "halide_profiler_enable_instance",
                              {profiler_instance}, Call::Extern);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {

        auto [new_extents, changed] = mutate_with_changes(op->extents);
        Expr condition = mutate(op->condition);

        bool can_fit_on_stack;
        Expr size = compute_allocation_size(new_extents, condition, op->type, op->name, can_fit_on_stack);
        internal_assert(size.type() == UInt(64));

        bool on_stack = can_fit_on_stack && !op->new_expr.defined();

        // Resolve the id once here. visit(Free) may fire at a different
        // producer-stack depth than this Allocate (the Allocate can be hoisted
        // while the Free stays inside an enclosing producer's body), so we
        // cache the id and have Free use it instead of re-querying stack.back().
        int idx;
        switch (classify(op->name)) {
        case Kind::ProfiledFunc:
            idx = get_func_entry_id(op->name);
            break;
        case Kind::NonProfiledFunc:
            // Attribute the stack size contribution to the deepest _profiled_ func.
            idx = stack.back();
            break;
        case Kind::NotAFunc:
            idx = -1;
            break;
        }

        func_alloc_sizes.push(op->name, {on_stack, size, idx});

        // compute_allocation_size() might return a zero size, if the allocation is
        // always conditionally false. remove_dead_allocations() is called after
        // inject_profiling() so this is a possible scenario.
        if (!is_const_zero(size) && on_stack && idx >= 0) {
            auto int_size = as_const_uint(size);
            internal_assert(int_size);  // Stack size is always a const int
            func_stack_current[idx] += *int_size;
            func_stack_peak[idx] = std::max(func_stack_peak[idx], func_stack_current[idx]);
            debug(3) << "  Allocation on stack: " << op->name
                     << "(" << size << ") in pipeline " << names.pipeline_name
                     << "; current: " << func_stack_current[idx]
                     << "; peak: " << func_stack_peak[idx] << "\n";
        }

        vector<Stmt> tasks;
        bool track_heap_allocation = !is_const_zero(size) && !on_stack && profiling_memory;
        if (track_heap_allocation) {
            debug(3) << "  Allocation on heap: " << op->name
                     << "(" << size << ") in pipeline "
                     << names.pipeline_name << "\n";

            tasks.push_back(set_current_func(names.malloc_id));
            tasks.push_back(Evaluate::make(Call::make(Int(32), "halide_profiler_memory_allocate",
                                                      {profiler_instance, idx, size}, Call::Extern)));
        }

        Stmt body = mutate(op->body);

        Expr new_expr;
        Stmt stmt;
        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr);
        }
        if (!changed &&
            body.same_as(op->body) &&
            condition.same_as(op->condition) &&
            new_expr.same_as(op->new_expr)) {
            stmt = op;
        } else {
            stmt = Allocate::make(op->name, op->type, op->memory_type,
                                  new_extents, condition, body, new_expr,
                                  op->free_function, op->padding);
        }

        tasks.push_back(stmt);

        return Block::make(tasks);
    }

    Stmt visit(const Free *op) override {
        AllocSize alloc = func_alloc_sizes.get(op->name);
        internal_assert(alloc.size.type() == UInt(64));
        func_alloc_sizes.pop(op->name);

        Stmt stmt = IRMutator::visit(op);

        if (!is_const_zero(alloc.size)) {
            int idx = alloc.id;
            if (!alloc.on_stack) {
                if (profiling_memory) {
                    debug(3) << "  Free on heap: " << op->name << "(" << alloc.size << ") in pipeline " << names.pipeline_name << "\n";

                    vector<Stmt> tasks{
                        set_current_func(names.free_id),
                        Evaluate::make(Call::make(Int(32), "halide_profiler_memory_free",
                                                  {profiler_instance, idx, alloc.size}, Call::Extern)),
                        stmt,
                        set_current_func(stack.back())};

                    stmt = Block::make(tasks);
                }
            } else {
                auto int_size = as_const_uint(alloc.size);
                internal_assert(int_size);

                if (idx >= 0) {
                    func_stack_current[idx] -= *int_size;
                    debug(3) << "  Free on stack: " << op->name
                             << "(" << alloc.size << ") in pipeline " << names.pipeline_name
                             << "; current: " << func_stack_current[idx]
                             << "; peak: " << func_stack_peak[idx] << "\n";
                }
            }
        }
        return stmt;
    }

    Stmt visit(const ProducerConsumer *op) override {
        Stmt body;
        if (classify(op->name) == Kind::ProfiledFunc) {
            if (op->is_producer) {
                int idx = get_func_entry_id(op->name);
                stack.push_back(idx);
                Stmt set_current = set_current_func(idx);
                body = Block::make(set_current, mutate(op->body));
                stack.pop_back();
            } else {
                Stmt set_current = set_current_func(stack.back());
                body = Block::make(set_current, mutate(op->body));
            }
            return ProducerConsumer::make(op->name, op->is_producer, body);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit_parallel_task(Stmt s) {
        int old = most_recently_set_func;
        if (const Fork *f = s.as<Fork>()) {
            s = Fork::make(visit_parallel_task(f->first), visit_parallel_task(f->rest));
        } else if (const Acquire *a = s.as<Acquire>()) {
            s = Acquire::make(a->semaphore, a->count, visit_parallel_task(a->body));
        } else {
            s = activate_thread(mutate(s));
        }
        if (most_recently_set_func != old) {
            most_recently_set_func = -1;
        }
        return s;
    }

    Stmt visit(const Acquire *op) override {
        Stmt s = visit_parallel_task(op);
        return suspend_thread(s);
    }

    Stmt visit(const Fork *op) override {
        ScopedValue<bool> bind(in_fork, true);
        Stmt s = visit_parallel_task(op);
        return suspend_thread(s);
    }

    Stmt visit(const For *op) override {
        Stmt body = op->body;

        // The for loop indicates a device transition or a
        // parallel job launch. Decrement the number of active
        // threads outside the loop, and increment it inside the
        // body.
        bool update_active_threads = (op->device_api == DeviceAPI::Hexagon ||
                                      op->is_unordered_parallel());

        ScopedValue<bool> bind_in_parallel(in_parallel, in_parallel || op->is_unordered_parallel());

        bool leaf_task = false;
        if (update_active_threads) {

            class ContainsParallelOrBlockingNode : public IRVisitor {
                using IRVisitor::visit;
                void visit(const For *op) override {
                    result |= (op->is_unordered_parallel() ||
                               op->device_api != DeviceAPI::None);
                    IRVisitor::visit(op);
                }
                void visit(const Fork *op) override {
                    result = true;
                }
                void visit(const Acquire *op) override {
                    result = true;
                }

            public:
                bool result = false;
            } contains_parallel_or_blocking_node;

            body.accept(&contains_parallel_or_blocking_node);
            leaf_task = !contains_parallel_or_blocking_node.result;

            if (leaf_task) {
                // TODO: Shouldn't this be *outside* activate thread, so that the func setting is done by a single leaf?
                body = claim_sampling_token(body, profiler_shared_sampling_token, profiler_local_sampling_token);
            }

            body = activate_thread(body);
        }
        ScopedValue<bool> bind_leaf_task(in_leaf_task, in_leaf_task || leaf_task);

        int old = most_recently_set_func;

        // We profile by storing a token to global memory, so don't enter GPU loops
        if (op->device_api == DeviceAPI::Hexagon) {
            // TODO: This is for all offload targets that support
            // limited internal profiling, which is currently just
            // hexagon. We don't support per-func stats remotely,
            // which means we can't do memory accounting.
            bool old_profiling_memory = profiling_memory;
            profiling_memory = false;
            body = mutate(body);
            profiling_memory = old_profiling_memory;

            // Get the profiler state pointer from scratch inside the
            // kernel. There will be a separate copy of the state on
            // the DSP that the host side will periodically query.
            Expr get_state = Call::make(Handle(), "halide_hexagon_remote_profiler_get_global_instance", {}, Call::Extern);
            body = substitute(names.profiler_instance, Variable::make(Handle(), names.hvx_profiler_instance), body);
            body = LetStmt::make(names.hvx_profiler_instance, get_state, body);
        } else if (op->device_api == DeviceAPI::None ||
                   op->device_api == DeviceAPI::Host) {
            body = mutate(body);
        } else {
            body = op->body;
        }

        if (old != most_recently_set_func) {
            most_recently_set_func = -1;
        }

        Stmt stmt = For::make(op->name, op->min, op->max, op->for_type, op->partition_policy, op->device_api, body);

        // Force a device sync after every GPU kernel launch. Kernel
        // launches are asynchronous, so without this the actual compute
        // time gets billed to whatever blocking host operation runs
        // next (typically a halide_copy_to_host, or the end-of-pipeline
        // device-free) and the profiler attributes time to the wrong
        // row. We only do this when building with -profile (this whole
        // pass only runs in that case), and only at the outermost GPU
        // loop — inner GPU loops live inside the kernel body, which
        // this pass does not descend into. The trade-off is that any
        // overlap (host work, future kernel launches) that the
        // schedule was relying on disappears, so the absolute time of
        // a profiled GPU build is biased upward; that's documented in
        // HalideRuntime.h.
        if (op->device_api != DeviceAPI::None &&
            op->device_api != DeviceAPI::Host &&
            op->device_api != DeviceAPI::Hexagon) {
            Expr device_interface = make_device_interface_call(op->device_api);
            Stmt sync = call_extern_and_assert("halide_device_sync_global", {device_interface});
            stmt = Block::make(stmt, sync);
        }

        if (update_active_threads) {
            if (Internal::is_gpu(op->for_type)) {
                stmt = suspend_thread_but_keep_task_id(stmt);
            } else {
                stmt = suspend_thread(stmt);
            }
        }

        return stmt;
    }

    Stmt visit(const IfThenElse *op) override {
        int old = most_recently_set_func;
        Expr condition = mutate(op->condition);
        Stmt then_case = mutate(op->then_case);
        int func_computed_in_then = most_recently_set_func;
        most_recently_set_func = old;
        Stmt else_case = mutate(op->else_case);
        if (most_recently_set_func != func_computed_in_then) {
            most_recently_set_func = -1;
        }
        if (condition.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        }
        return IfThenElse::make(std::move(condition), std::move(then_case), std::move(else_case));
    }

    // Pattern emitted by InjectHostDevBufferCopies:
    //   let err = halide_copy_to_{host,device}(buf, ...) in
    //     assert(err == 0)
    //     <rest>
    // We bill the copy as its own synthetic Func ("<name> (copy to {host,device})")
    // so its time shows up as its own line in the profile. The synthetic func
    // is active for the duration of the copy plus the assert and (for
    // copy_to_device) a device-sync barrier; after that we go back to whatever
    // producer is on the stack.
    Stmt inject_buffer_copy_timing(const LetStmt *op, const Call *call) {
        const Variable *var = call->args.front().as<Variable>();
        internal_assert(var)
            << "Expected to find a variable as first argument of the function call " << call->name << ".\n";
        std::string buffer_name = var->name;
        internal_assert(ends_with(buffer_name, ".buffer"))
            << "Expected to find a variable ending in .buffer as first argument to function call " << call->name << "\n";
        buffer_name = buffer_name.substr(0, buffer_name.size() - 7);

        bool to_device = call->name == "halide_copy_to_device";
        const char *tag = to_device ? " (copy to device)" : " (copy to host)";
        // Parent the synthetic copy "Func" to whichever producer it sits
        // inside, so it nests under that producer in the timeline view.
        // PreAllocateEntries allocates these ids eagerly using the same
        // parent, so id_for_entry here just looks up the existing id.
        // overhead_id sits at the bottom of the stack as a sentinel — at
        // the outermost level it stands in for "no enclosing producer".
        int parent_id = stack.back();
        if (parent_id == names.overhead_id) {
            parent_id = -1;
        }
        int copy_id = names.id_for_entry(buffer_name + tag, parent_id);
        // Bump a counter per copy invocation so tests/reports can see
        // exactly how many times it fired (the sample-based `time` is too
        // coarse for fast copies).
        Stmt count_call = Evaluate::make(
            Call::make(Int(32), "halide_profiler_count_host_device_copy",
                       {profiler_instance, make_const(Int(32), copy_id)}, Call::Extern));
        Stmt start_profiler = Block::make(count_call, set_current_func(copy_id));

        // The copy is followed by an assert; we wrap both (and, for
        // copy_to_device, the subsequent device sync) in the timed window.
        const AssertStmt *copy_assert = nullptr;
        Stmt other;
        if (const Block *block = op->body.as<Block>()) {
            copy_assert = block->first.as<AssertStmt>();
            if (copy_assert) {
                other = block->rest;
            }
        } else {
            copy_assert = op->body.as<AssertStmt>();
        }
        internal_assert(copy_assert) << "No assert found after buffer copy.";

        std::vector<Stmt> steps;
        steps.push_back(AssertStmt::make(copy_assert->condition, copy_assert->message));
        if (to_device) {
            // Last arg to copy_to_device is the device interface.
            Expr device_interface = call->args.back();
            steps.push_back(call_extern_and_assert("halide_device_sync_global", {device_interface}));
        }
        steps.push_back(set_current_func(stack.back()));
        if (other.defined()) {
            steps.push_back(mutate(other));
        }
        return Block::make(start_profiler,
                           LetStmt::make(op->name, mutate(op->value),
                                         Block::make(steps)));
    }

    Stmt visit(const LetStmt *op) override {
        if (const Call *call = op->value.as<Call>()) {
            if (call->name == "halide_copy_to_host" || call->name == "halide_copy_to_device") {
                return inject_buffer_copy_timing(op, call);
            }
        }

        Stmt body = mutate(op->body);
        Expr value = mutate(op->value);
        if (body.same_as(op->body) && value.same_as(op->value)) {
            return op;
        }
        return LetStmt::make(op->name, value, body);
    }
};

}  // namespace

// =============================================================================
// inline_marker resolution
// =============================================================================
//
// Inline.cpp leaves an inline_marker intrinsic at every inlined call site
// (one per Func inlining, nested when a chain of Funcs is inlined into the
// same site). This pass walks each Provide, replaces the markers with their
// bodies, and stamps down a declare_inlined intrinsic recording the inlining
// graph for the Provide. PreAllocateEntries in inject_profiling then
// allocates per-entry ids from those graphs.
//
// Stmt-level CSE inside Inline.cpp can hoist common subexpressions —
// including ones containing markers — out into LetStmts wrapping the
// Provide, so we have to deal with that too. We treat each such LetStmt's
// RHS as an "unrooted subgraph": its top-level markers are recorded as the
// let's subgraph roots, and each use of the let var contributes an edge
// from the current parent context to those roots. This handles arbitrarily
// nested let chains and shared subexpressions without substituting let
// values into every use site (which would be quadratic for large bodies).
//
// Lets that the Provide never references stay in the IR with their markers
// stripped (so codegen doesn't trip on them), but contribute no nodes to
// the graph.

namespace {

bool expr_contains_marker(const Expr &e) {
    bool found = false;
    visit_with(e, [&](auto *self, const Call *op) {
        if (op->is_intrinsic({Call::inline_marker, Call::extern_stage_marker})) {
            found = true;
        }
        self->visit_base(op);
    });
    return found;
}

// Mutator that walks an entire let-chain + Provide subtree as one unit:
//   - On an inline_marker call: replaces it with its body, registering a
//     node and an edge (or recording the node as a subgraph root, if we're
//     currently walking a let RHS).
//   - On a Let or LetStmt: walks the value through `this` with the let-
//     root parent sentinel set so the value's top-level markers become
//     this let's subgraph roots, records the roots, then walks the body.
//   - On a Variable: if the name matches a let we've already processed,
//     emits edges from the current parent to that let's subgraph roots
//     (or forwards the roots to the enclosing let, if we're inside one).
class BuildInlineGraph : public IRMutator {
public:
    // Output: the graph built during mutate(), plus the name of the Provide
    // it surrounds (captured during the walk so the caller doesn't have to
    // re-walk the IR to find it).
    std::map<int, Expr> nodes_by_id;
    struct Edge {
        int caller, callee;
    };
    std::vector<Edge> edges;
    std::string provide_name;

protected:
    // Sentinel parent value meaning "we're walking a let RHS": markers
    // encountered with this parent become subgraph roots for the
    // surrounding let rather than children of an enclosing context. -1 is
    // already taken (means "the Provide node"), so use -2.
    static constexpr int let_root_sentinel = -2;

    int parent = -1;
    std::set<int> *current_let_roots = nullptr;

    // Internal dedup map: same inline_marker Expr (via CSE) collapses to one node.
    std::map<Expr, int, ExprCompare> nodes_by_expr;
    // Subgraph roots for each let, indexed by name.
    std::map<std::string, std::set<int>> let_roots;

    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::inline_marker)) {
            internal_assert(op->args.size() == 2);
            Expr e(op);
            auto [it, inserted] = nodes_by_expr.try_emplace(e, (int)nodes_by_expr.size());
            int id = it->second;
            if (inserted) {
                nodes_by_id[id] = e;
            }
            if (parent == let_root_sentinel) {
                current_let_roots->insert(id);
            } else {
                edges.emplace_back(Edge{parent, id});
            }
            ScopedValue<int> old(parent, id);
            return mutate(op->args[1]);
        } else if (op->is_intrinsic(Call::extern_stage_marker)) {
            // ScheduleFunctions tagged an extern call's value. The
            // extern stage's Halide name is in args[0]; use it as the
            // billing target for any inline_markers in the call args.
            // Strip the marker — only its first arg matters here.
            internal_assert(op->args.size() == 2);
            const StringImm *s = op->args[0].as<StringImm>();
            internal_assert(s) << op->args[0];
            if (provide_name.empty()) {
                provide_name = s->value;
            }
            return mutate(op->args[1]);
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Variable *op) override {
        auto it = let_roots.find(op->name);
        if (it != let_roots.end()) {
            if (parent == let_root_sentinel) {
                current_let_roots->insert(it->second.begin(), it->second.end());
            } else {
                for (int r : it->second) {
                    edges.emplace_back(Edge{parent, r});
                }
            }
        }
        return op;
    }

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        // Walk the chain top-down processing each value (so that later
        // values can resolve references to earlier let names), then walk
        // the body, then rebuild bottom-up. Iterative form so deep chains
        // don't blow the stack.
        struct Frame {
            const LetOrLetStmt *op;
            Expr new_value;
        };
        std::vector<Frame> frames;
        decltype(op->body) body;
        do {
            // Walk the value with parent = let_root_sentinel so its top-
            // level markers become this let's subgraph roots.
            std::set<int> roots;
            Expr new_value;
            {
                ScopedValue<int> sp(parent, let_root_sentinel);
                ScopedValue<std::set<int> *> sr(current_let_roots, &roots);
                new_value = mutate(op->value);
            }
            let_roots[op->name] = std::move(roots);
            frames.push_back({op, std::move(new_value)});
            body = op->body;
        } while ((op = body.template as<LetOrLetStmt>()));

        body = mutate(body);

        for (const auto &frame : reverse_view(frames)) {
            if (frame.new_value.same_as(frame.op->value) && body.same_as(frame.op->body)) {
                body = frame.op;
            } else {
                body = LetOrLetStmt::make(frame.op->name, frame.new_value, body);
            }
        }
        return body;
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Stmt visit(const Provide *op) override {
        provide_name = op->name;
        return IRMutator::visit(op);
    }
};

// Process a Stmt that's either a Provide or a chain of LetStmts ending in
// a Provide. Builds the inlining graph, strips markers, rebuilds, and
// emits a declare_inlined.
Stmt process_inlining_subtree(const Stmt &s) {

    BuildInlineGraph builder;

    Stmt rewritten = builder(s);

    if (builder.provide_name.empty()) {
        // Nothing claimed the inline_markers in this subtree — neither
        // a Provide nor an extern_stage_marker. The walk above has
        // already stripped the markers, so the IR is well-formed; we
        // just have no entry to bill the inlined calls to. Skip
        // emitting a declare_inlined.
        return rewritten;
    }

    // Now stamp down the declare_inlined intrinsic that encodes the graph via a
    // list of its nodes followed by a list of its edges.
    std::vector<Expr> intrin_args;
    intrin_args.reserve(1 + builder.nodes_by_id.size() + 2 * builder.edges.size());
    intrin_args.emplace_back(builder.provide_name);
    for (const auto &[id, marker] : builder.nodes_by_id) {
        intrin_args.push_back(marker.as<Call>()->args[0]);
    }
    for (const auto &edge : builder.edges) {
        // +1 to account for the provide node itself at index 0.
        intrin_args.push_back(edge.caller + 1);
        intrin_args.push_back(edge.callee + 1);
    }
    if (intrin_args.size() > 1) {
        Stmt decl = Evaluate::make(Call::make(Int(32), Call::declare_inlined,
                                              intrin_args, Call::Intrinsic));
        rewritten = Block::make(decl, rewritten);
    }
    return rewritten;
}

}  // namespace

Stmt resolve_inline_markers(const Stmt &s) {
    return mutate_with(
        s,  //
        [&](auto *self, const LetStmt *op) {
            // A LetStmt whose RHS carries a marker was hoisted out of
            // its Provide by CSE, or sits above an extern stage's call
            // (tagged with an extern_stage_marker). Process the chain
            // together with whatever it anchors to.
            if (expr_contains_marker(op->value)) {
                return process_inlining_subtree(Stmt(op));
            } else {
                return self->visit_base(op);
            }  //
        },                                    //
        [&](auto *self, const Provide *op) {  //
            return process_inlining_subtree(Stmt(op));
        },  //
        [&](auto *self, const Evaluate *op) {
            // An Evaluate of an expression containing a marker (e.g. an
            // extern call wrapped in extern_stage_marker that wasn't
            // bound to a let). Process it like a Provide.
            if (expr_contains_marker(op->value)) {
                return process_inlining_subtree(Stmt(op));
            } else {
                return self->visit_base(op);
            }
        });
}

// Drop declare_box_required_at_root intrinsics whose bounds couldn't be
// reduced to int32-representable form. Bounds inference for inlined Funcs
// happily produces such bounds (e.g. an index of `(c1 - c2) * c3` over a
// wide interval), and simplify materialises a `signed_integer_overflow`
// intrinsic for the offending sub-Expr; the box-required marker is a
// nice-to-have for the recompute-ratio report, so dropping it is safer
// than letting it reach codegen (which would `user_error`).
//
// The taint is tracked via a Scope of poisoned let-binding names so that
// a downstream reference to the simplifier-introduced let (e.g.
// `let t = signed_integer_overflow(N); ... declare_box_required_at_root(..., t)`)
// also drops the marker.
class DropPoisonedBoxRequired : public IRMutator {
    Scope<> poisoned;

    // True if `e` directly contains a signed_integer_overflow intrinsic, or
    // references a Variable currently in `poisoned`.
    bool is_poisoned(const Expr &e) const {
        bool result = false;
        visit_with(e,  //
                   [&](auto *self, const Call *op) {
                       if (op->is_intrinsic(Call::signed_integer_overflow)) {
                           result = true;
                       } else {
                           self->visit_base(op);
                       }  //
                   },
                   [&](auto *self, const Variable *op) {
                       if (poisoned.contains(op->name)) {
                           result = true;
                       }  //
                   });
        return result;
    }

    using IRMutator::visit;

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        // Walk the let chain top-down sniffing each value for poison, then
        // mutate the body, then rebuild bottom-up. Iterative form so deep
        // let chains don't blow the stack. We don't mutate the values —
        // declare_box_required_at_root only ever appears at stmt position,
        // so there's nothing inside a let value for us to rewrite.
        struct Frame {
            const LetOrLetStmt *op;
            ScopedBinding<> binding;
        };
        std::vector<Frame> frames;
        decltype(op->body) body;
        do {
            ScopedBinding<> binding(is_poisoned(op->value), poisoned, op->name);
            frames.push_back({op, std::move(binding)});
            body = op->body;
        } while ((op = body.template as<LetOrLetStmt>()));

        body = mutate(body);

        for (const auto &frame : reverse_view(frames)) {
            if (body.same_as(frame.op->body)) {
                body = frame.op;
            } else {
                body = LetOrLetStmt::make(frame.op->name, frame.op->value, body);
            }
        }
        return body;
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::declare_box_required_at_root)) {
            for (size_t i = 1; i < op->args.size(); i++) {
                if (is_poisoned(op->args[i])) {
                    return make_zero(op->type);
                }
            }
        }
        return IRMutator::visit(op);
    }
};

Stmt inject_profiling(const Stmt &stmt, const string &pipeline_name, const std::map<string, Function> &env, const Target &target) {
    Names names(pipeline_name, env);

    // 0) Drop any declare_box_required_at_root marker whose bounds Expr
    //    transitively depends on a signed_integer_overflow intrinsic
    //    (introduced by simplify when an inlined Func's bounds couldn't
    //    be represented in int32). These markers are nice-to-have stats
    //    only — dropping them is safer than letting them reach codegen.
    Stmt s = DropPoisonedBoxRequired()(stmt);

    // 1) Allocate an id for every entry (each producer node and each
    //    inlined call site), and rewrite declare_inlined intrinsics to
    //    hold those resolved ids directly. After this, names.entry_info
    //    is the full set of entries we'll report on.
    s = PreAllocateEntries(names, env)(s);

    // 2) Inject the counter-update calls for stats (loads, stores,
    //    realizations, parallel loops, inlined call billing, etc.).
    InjectCounters injector(names, env);
    s = injector(s);
    // Fold the approximated-entry set into each EntryInfo's flags
    // bitfield so it gets passed in via the per-entry constants array
    // at halide_profiler_instance_start, alongside the other initial-
    // flag bits set by id_for_entry.
    for (int id : injector.approximated_entries) {
        names.entry_info[id].flags |= halide_profiler_func_flag_counters_approximated;
    }

    // 3) Inject the rest of the profiler scaffolding: thread activation,
    //    memory tracking, current-func tracking, copy-to-host/device timing.
    InjectProfiling profiling(names, env);
    s = profiling(s);

    int num_funcs = names.num_ids();

    Expr instance = Variable::make(Handle(), names.profiler_instance);

    Expr func_names_buf = Variable::make(Handle(), names.profiler_func_names);
    Expr func_parents_buf = Variable::make(Handle(), names.profiler_func_parents);
    Expr func_canonical_ids_buf = Variable::make(Handle(), names.profiler_func_canonical_ids);
    Expr func_kinds_buf = Variable::make(Handle(), names.profiler_func_kinds);
    Expr func_flags_buf = Variable::make(Handle(), names.profiler_func_flags);
    Expr func_buffer_func_ids_buf = Variable::make(Handle(), names.profiler_func_buffer_func_ids);

    Expr start_profiler = Call::make(Int(32), "halide_profiler_instance_start",
                                     {pipeline_name,
                                      num_funcs,
                                      func_names_buf,
                                      func_parents_buf,
                                      func_canonical_ids_buf,
                                      func_kinds_buf,
                                      func_flags_buf,
                                      func_buffer_func_ids_buf,
                                      make_const(UInt(64), target.natural_vector_size(UInt(8))),
                                      instance},
                                     Call::Extern);

    Expr profiler_start_error_code = Variable::make(Int(32), names.profiler_start_error_code);

    Expr stop_profiler = Call::make(Handle(), Call::register_destructor,
                                    {Expr("halide_profiler_instance_end"), instance}, Call::Intrinsic);

    bool no_stack_alloc = profiling.func_stack_peak.empty();
    if (!no_stack_alloc) {
        Expr func_stack_peak_buf = Variable::make(Handle(), names.profiler_func_stack_peak_buf);

        Stmt update_stack = Evaluate::make(Call::make(Int(32), "halide_profiler_stack_peak_update",
                                                      {instance, func_stack_peak_buf}, Call::Extern));
        s = Block::make(update_stack, s);
    }

    s = profiling.activate_main_thread(s);

    // Initialize the shared sampling token
    Expr shared_sampling_token_var = Variable::make(Handle(), names.profiler_shared_sampling_token);
    Expr init_sampling_token =
        Call::make(Int(32), "halide_profiler_init_sampling_token", {shared_sampling_token_var, 0}, Call::Extern);
    s = Block::make({Evaluate::make(init_sampling_token), s});
    s = LetStmt::make(names.profiler_shared_sampling_token,
                      Call::make(Handle(), Call::alloca, {Int(32).bytes()}, Call::Intrinsic), s);

    // If there was a problem starting the profiler, it will call an
    // appropriate halide error function and then return the
    // (negative) error code as the token.
    s = Block::make(AssertStmt::make(profiler_start_error_code == 0, profiler_start_error_code), s);
    s = LetStmt::make(names.profiler_start_error_code, start_profiler, s);

    if (!no_stack_alloc) {
        for (int i = num_funcs - 1; i >= 0; --i) {
            s = Block::make(Store::make(names.profiler_func_stack_peak_buf,
                                        make_const(UInt(64), profiling.func_stack_peak[i]),
                                        i, Parameter(), const_true(), ModulusRemainder()),
                            s);
        }
        s = Block::make(s, Free::make(names.profiler_func_stack_peak_buf));
        s = Allocate::make(names.profiler_func_stack_peak_buf, UInt(64),
                           MemoryType::Auto, {num_funcs}, const_true(), s);
    }

    std::vector<Expr> func_names(num_funcs);
    std::vector<Expr> func_parents(num_funcs);
    std::vector<Expr> func_canonical_ids(num_funcs);
    std::vector<Expr> func_kinds(num_funcs);
    std::vector<Expr> func_flags(num_funcs);
    std::vector<Expr> func_buffer_func_ids(num_funcs);
    for (int i = 0; i < num_funcs; i++) {
        const auto &info = names.entry_info[i];
        func_names[i] = info.name;
        func_parents[i] = info.parent_id;
        func_canonical_ids[i] = info.canonical_id;
        func_kinds[i] = make_const(UInt(8), (uint8_t)info.kind);
        func_flags[i] = make_const(UInt(8), info.flags);
        func_buffer_func_ids[i] = info.buffer_func_id;
    }

    s = LetStmt::make(names.profiler_func_names, Call::make(Handle(), Call::make_struct, func_names, Call::Intrinsic), s);
    s = LetStmt::make(names.profiler_func_parents, Call::make(Handle(), Call::make_struct, func_parents, Call::Intrinsic), s);
    s = LetStmt::make(names.profiler_func_canonical_ids, Call::make(Handle(), Call::make_struct, func_canonical_ids, Call::Intrinsic), s);
    s = LetStmt::make(names.profiler_func_kinds, Call::make(Handle(), Call::make_struct, func_kinds, Call::Intrinsic), s);
    s = LetStmt::make(names.profiler_func_flags, Call::make(Handle(), Call::make_struct, func_flags, Call::Intrinsic), s);
    s = LetStmt::make(names.profiler_func_buffer_func_ids, Call::make(Handle(), Call::make_struct, func_buffer_func_ids, Call::Intrinsic), s);
    s = Block::make(Evaluate::make(stop_profiler), s);

    // Allocate memory for the profiler instance state

    // Check there isn't going to be end-of-struct padding to worry about.
    static_assert((sizeof(halide_profiler_func_stats) & 7) == 0);

    const int instance_size_bytes = sizeof(halide_profiler_instance_state) + num_funcs * sizeof(halide_profiler_func_stats);

    s = Allocate::make(names.profiler_instance, UInt(64), MemoryType::Auto,
                       {(instance_size_bytes + 7) / 8}, const_true(), s);

    // We have nested definitions of the sampling token
    s = uniquify_variable_names(s);

    return s;
}

}  // namespace Internal
}  // namespace Halide
