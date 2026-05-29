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

// Profiling injection. Runs after bounds inference, before storage
// flattening, when the "profile" target feature is on.
//
// Time is collected by sampling: a background thread periodically reads
// a "current func" word that the generated code writes when the pipeline
// transitions between Funcs. We additionally bill memory allocations,
// stack peaks, active-thread counts, and host<->device copy times.
//
// Two sub-passes:
//
//  1) PreAllocateEntries — walks the IR and assigns each profiled
//     producer (plus the synthetic copy-to-host/device sites and
//     hoist_storage allocations) an entry id, matching its eventual slot
//     in the runtime's halide_profiler_func_stats array. The walk order
//     fixes the id order, which the reporter then walks as a tree.
//
//  2) InjectProfiling — emits the runtime calls
//     (halide_profiler_set_current_func, _memory_allocate/_free,
//     _incr/decr_active_threads, sampling-token plumbing,
//     copy-to-host/device timing) and wraps the whole IR with
//     halide_profiler_instance_start / _end.
//
// Entries (rows in the stats array):
//
// Most Funcs map to one entry. An unscheduled Func with an update def
// reached from multiple callers gets a separate Realize/Produce per
// caller, hence a separate entry per appearance — letting per-caller
// time%, peak memory, etc. show separately rather than collapsing them.
// `canonical_id` points each instance at the first id allocated for its
// name so the reporter can roll them back up "by Func" if it chooses.

namespace {

// Unique names for the IR symbols this pass introduces, plus the entry
// table (one EntryInfo per row in halide_profiler_func_stats).
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
    std::string profiler_func_buffer_func_ids;
    std::string profiler_start_error_code;

    // IDs 0-3 are reserved for bookkeeping slots, in this order.
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
          profiler_func_buffer_func_ids(unique_name("profiler_func_buffer_func_ids")),
          profiler_start_error_code(unique_name("profiler_start_error_code")) {

        // Reserve the bookkeeping slots first so their ids match the
        // *_id constants above.
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

    struct EntryInfo {
        // Display name shown in the report. Differs from ir_name only
        // when a Function::profiler_display_name override is set.
        std::string name;
        // IR-level Func name, used to dedup entries.
        std::string ir_name;
        int parent_id;     // immediate parent entry, or -1 at root
        int canonical_id;  // first entry id allocated for this ir_name
        halide_profiler_func_kind kind;
        // For copy synthetics, the canonical id of the Func whose
        // buffer is being copied; -1 otherwise.
        int buffer_func_id;
    };
    std::vector<EntryInfo> entry_info;
    std::map<std::string, int> canonical_id_for_name;
    // (parent_id, ir_name) -> id. Keyed on IR name so two appearances of
    // a Func that share a profiler_display_name still dedup correctly.
    std::map<std::pair<int, std::string>, int> entry_map;

    // Get or allocate the id for (ir_name, parent_id). Same name under
    // different parents gets different ids.
    int id_for_entry(const std::string &ir_name, int parent_id,
                     halide_profiler_func_kind kind = halide_profiler_func_kind_func,
                     int buffer_func_id = -1) {
        auto [it, inserted] = entry_map.try_emplace({parent_id, ir_name}, (int)entry_info.size());
        if (inserted) {
            int canon = canonical_id_for_name.try_emplace(ir_name, it->second).first->second;
            std::string display_name = ir_name;
            if (kind == halide_profiler_func_kind_func) {
                auto eit = env.find(ir_name);
                if (eit != env.end()) {
                    const Function &f = eit->second;
                    if (!f.profiler_display_name().empty()) {
                        display_name = f.profiler_display_name();
                    }
                }
            }
            entry_info.push_back({display_name, ir_name, parent_id, canon, kind, buffer_func_id});
        }
        return it->second;
    }

    // Shorthand for the root-level (parent = -1) entry for a name.
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

Expr compute_allocation_size(const vector<Expr> &extents,
                             const Expr &condition,
                             const Type &type,
                             const std::string &name,
                             bool &can_fit_on_stack) {
    can_fit_on_stack = true;

    Expr cond = simplify(condition);
    if (is_const_zero(cond)) {
        return make_zero(UInt(64));
    }

    int64_t constant_size = Allocate::constant_allocation_size(extents, name);
    if (constant_size > 0) {
        int64_t stack_bytes = constant_size * type.bytes();
        if (can_allocation_fit_on_stack(stack_bytes)) {
            return make_const(UInt(64), stack_bytes);
        }
    }

    internal_assert(!extents.empty());

    can_fit_on_stack = false;
    Expr size = cast<uint64_t>(extents[0]);
    for (size_t i = 1; i < extents.size(); i++) {
        size *= extents[i];
    }
    size = simplify(Select::make(condition, size * type.bytes(), make_zero(UInt(64))));
    return size;
}

// Unwrap a Broadcast(...) wrapper from an arg, then extract the Func name.
// declare_box_required_at_root / declare_stage carry the Func name as a
// StringImm in their first arg — the name is just a label for the
// profiler report, not a symbol that exists in scope, so using a
// StringImm keeps it from being matched by passes like
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
// producer, parented to the surrounding producer).
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

    using IRMutator::visit;

    Stmt visit(const Allocate *op) override {
        int id = -1;
        std::string fname = names.prefix(op->name);
        auto it = env.find(fname);
        if (it != env.end() && !it->second.should_not_profile()) {
            id = names.id_for_entry(fname, producer_id);
        }
        Stmt result = IRMutator::visit(op);
        // If no Produce at this scope minted the same id, this is a
        // hoist_storage allocation site — give it the allocation kind.
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
        // Mint entries for copy-to-host/device sites so they get ids in
        // IR order (the report renders by id).
        if (op->name == "halide_copy_to_host" || op->name == "halide_copy_to_device") {
            if (!op->args.empty()) {
                if (const Variable *v = op->args.front().as<Variable>()) {
                    if (ends_with(v->name, ".buffer")) {
                        std::string buffer_name = v->name.substr(0, v->name.size() - 7);
                        bool to_device = op->name == "halide_copy_to_device";
                        const char *tag = to_device ? " (copy to device)" : " (copy to host)";
                        halide_profiler_func_kind kind =
                            to_device ? halide_profiler_func_kind_copy_to_device : halide_profiler_func_kind_copy_to_host;
                        int buffer_func_id = -1;
                        auto it = names.canonical_id_for_name.find(buffer_name);
                        if (it != names.canonical_id_for_name.end()) {
                            buffer_func_id = it->second;
                        }
                        names.id_for_entry(buffer_name + tag, producer_id, kind, buffer_func_id);
                    }
                }
            }
        }
        return IRMutator::visit(op);
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
        // Index them by IR name so declare_box_required_root (which
        // carries the IR-level Func name from ScheduleFunctions) can
        // find all entries for a Func.
        for (int i = 0; i < names.num_ids(); i++) {
            entries_by_name[names.entry_info[i].ir_name].push_back(i);
        }
    }

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
    // - halide_profiler_update_counters in profiler_inlined.cpp
    // - the fields of halide_profiler_func_stats in HalideRuntime.h
    // - the block of code that prints counters to json in profiler_common.cpp
    enum { MemoryTotal = 0,
           NumAllocs,
           ParallelLoops,
           ParallelTasks,
           PointsRequiredAtRoot,
           PointsComputed };

    static constexpr int num_counters = PointsComputed + 1;

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
    // branches when the condition is impure).
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
        if (op->is_intrinsic(Call::declare_box_required_at_root)) {
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
        } else if (op->is_intrinsic(Call::declare_allocation)) {
            internal_assert(op->args.size() == 3);
            std::string fname = names.prefix(handle_name(op->args[0]));
            auto eit = env.find(fname);
            if (eit != env.end() && !eit->second.should_not_profile()) {
                int id = names.id_for_entry(fname, producer_id);
                counters[id].count(NumAllocs);
                counters[id].count(MemoryTotal, cast(UInt(64), op->args[1]));
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

    // True for Stores to buffers we want to bill: a Func's own storage, or
    // an output parameter. False for internal bookkeeping buffers like
    // storage-folding head trackers, async semaphores, and sampling
    // tokens — we don't want their stores inflating points_computed.
    bool is_real_data_buffer(const Store *op) const {
        return op->param.defined() || is_func(names.prefix(op->name));
    }

    Stmt visit(const Store *op) override {
        if (is_real_data_buffer(op)) {
            std::string f = names.prefix(op->name);
            // Stores in a producer block are to the Func being produced, so
            // bill them to the current producer's entry id. (That's the
            // right entry even if f has multiple entries elsewhere.)
            int id = (producer_id >= 0 && names.entry_info[producer_id].ir_name == f) ?
                         producer_id :
                         names.id_for_name(f);
            Counters &c = counters[id];
            int lanes = op->value.type().lanes();
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

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            // One entry per producer node, parented to the surrounding
            // producer. See file-level comment for why this matters.
            int id = names.id_for_entry(op->name, producer_id);
            ScopedValue<int> old(producer_id, id);
            return IRMutator::visit(op);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {
        // Bill heap allocations to NumAllocs and MemoryTotal.
        std::string fname = names.prefix(op->name);
        auto eit = env.find(fname);
        if (eit != env.end() && !eit->second.should_not_profile()) {
            bool can_fit_on_stack;
            Expr size = compute_allocation_size(op->extents, op->condition,
                                                op->type, op->name, can_fit_on_stack);
            bool on_stack = can_fit_on_stack && !op->new_expr.defined();
            if (!is_const_zero(size) && !on_stack) {
                int id = names.id_for_entry(fname, producer_id);
                counters[id].count(NumAllocs, cast(UInt(64), op->condition));
                counters[id].count(MemoryTotal, size);
            }
        }
        return IRMutator::visit(op);
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
        // Entry id resolved at Allocate time; Free uses the cached value
        // since the producer stack may differ at the two sites.
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

    // Resolve a Func name to the entry id PreAllocateEntries minted for
    // it under the currently-active producer chain.
    int get_func_entry_id(const string &name) {
        int parent = stack.back();
        // overhead_id is a sentinel at stack bottom; treat it as "no parent".
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
        // Inlined to a single store.
        Stmt s = Evaluate::make(Call::make(Int(32), "halide_profiler_set_current_func",
                                           {profiler_instance, id, last_arg}, Call::Extern));

        return s;
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::profiling_enable_instance_marker)) {
            // End of the bounds-query prelude — start collecting samples.
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

        // Resolve and cache the entry id here so visit(Free) can use it;
        // Allocate may have been hoisted out of the producer that
        // surrounds Free.
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
            // Ignore allocations that don't correspond to a Func
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
        bool track_heap_allocation = !is_const_zero(size) && !on_stack && profiling_memory && idx >= 0;
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
                if (profiling_memory && idx >= 0) {
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

        // Device transitions and parallel launches need an active-thread
        // bracket around the body (incremented inside, decremented out).
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

            // Use the DSP-side copy of the profiler state.
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

        // Sync after each outermost GPU launch so kernel time is billed
        // to the right row rather than to a later blocking host call.
        // This costs any overlap the schedule was getting; see the
        // -profile target-feature doc in HalideRuntime.h.
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

    // Bill a halide_copy_to_{host,device} call (and its trailing assert,
    // plus a device sync for copy_to_device) as a synthetic Func so it
    // gets its own row in the report. InjectHostDevBufferCopies emits it
    // as `let err = halide_copy_to_*(buf, ...) in assert(err == 0); ...`.
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
        int parent_id = stack.back();
        if (parent_id == names.overhead_id) {
            parent_id = -1;
        }
        int copy_id = names.id_for_entry(buffer_name + tag, parent_id);
        Stmt start_profiler = set_current_func(copy_id);

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

Stmt inject_profiling(const Stmt &stmt, const string &pipeline_name, const std::map<string, Function> &env, const Target &target) {
    Names names(pipeline_name, env);

    // 1) Allocate an id for every entry. After this, names.entry_info
    //    is the full set of entries we'll report on.
    Stmt s = PreAllocateEntries(names, env)(stmt);

    // 2) Inject the counter-update calls for stats (parallel loops,
    //    points computed, etc.).
    InjectCounters injector(names, env);
    s = injector(s);

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
    Expr func_buffer_func_ids_buf = Variable::make(Handle(), names.profiler_func_buffer_func_ids);

    Expr start_profiler = Call::make(Int(32), "halide_profiler_instance_start",
                                     {pipeline_name,
                                      num_funcs,
                                      func_names_buf,
                                      func_parents_buf,
                                      func_canonical_ids_buf,
                                      func_kinds_buf,
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
    std::vector<Expr> func_buffer_func_ids(num_funcs);
    for (int i = 0; i < num_funcs; i++) {
        const auto &info = names.entry_info[i];
        func_names[i] = info.name;
        func_parents[i] = info.parent_id;
        func_canonical_ids[i] = info.canonical_id;
        func_kinds[i] = make_const(Int(32), (int)info.kind);
        func_buffer_func_ids[i] = info.buffer_func_id;
    }

    s = LetStmt::make(names.profiler_func_names, Call::make(Handle(), Call::make_struct, func_names, Call::Intrinsic), s);
    s = LetStmt::make(names.profiler_func_parents, Call::make(Handle(), Call::make_struct, func_parents, Call::Intrinsic), s);
    s = LetStmt::make(names.profiler_func_canonical_ids, Call::make(Handle(), Call::make_struct, func_canonical_ids, Call::Intrinsic), s);
    s = LetStmt::make(names.profiler_func_kinds, Call::make(Handle(), Call::make_struct, func_kinds, Call::Intrinsic), s);
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
