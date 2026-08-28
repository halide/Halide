#ifndef HALIDE_SCHEDULE_EDITOR_H
#define HALIDE_SCHEDULE_EDITOR_H

/** \file
 *
 * Defines ScheduleEditor, a utility for building and editing the schedule of a
 * Halide pipeline as an ordered list of scheduling directives.
 *
 * A schedule is normally applied by calling scheduling methods directly on a
 * Func, which mutates its Stage's schedule and cannot be undone. ScheduleEditor
 * instead records the schedule as a list of ScheduleDirective values, one per
 * scheduling call (e.g. "blur_y.update(0).split(y, yo, yi, 8)"). The list can
 * be edited freely, then replayed onto a set of Funcs with apply(), or used to
 * rebuild a freshly scheduled Pipeline with materialize(). Pass directives() to
 * a ScheduleAnalyzer to print the list as C++ or serialize it to JSON.
 */

#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Expr.h"
#include "Func.h"
#include "IntrusivePtr.h"
#include "Pipeline.h"
#include "Schedule.h"
#include "Var.h"

namespace Halide {

/** A reference to a loop variable by name, remembering whether it came from a
 * reduction domain. Can be constructed from a name, a Var, an RVar, or a
 * VarOrRVar. */
struct VarSpec {
    std::string name;
    bool is_rvar = false;

    VarSpec() = default;
    VarSpec(const char *n)
        : name(n) {
    }
    VarSpec(std::string n, bool is_rvar = false)
        : name(std::move(n)), is_rvar(is_rvar) {
    }
    VarSpec(const Var &v)
        : name(v.name()), is_rvar(false) {
    }
    VarSpec(const RVar &r)
        : name(r.name()), is_rvar(true) {
    }
    VarSpec(const VarOrRVar &v)
        : name(v.name()), is_rvar(v.is_rvar) {
    }

    /** Reconstruct the corresponding VarOrRVar from the stored name. */
    VarOrRVar realize() const {
        return {name, is_rvar};
    }
};

/** A single scheduling method call, stored as data: which method (kind), which
 * Func and stage it applies to, and its operands. These are normally created
 * with the named constructors below or through ScheduleEditor's fluent
 * interface, rather than by setting the fields directly.
 *
 * Which fields are used depends on the kind; each named constructor documents
 * the fields it sets. */
struct ScheduleDirective {
    enum class Kind {
        // Stage-level (apply to the pure definition or an update stage):
        Split,
        Fuse,
        Rename,
        Reorder,
        Tile,
        Serial,
        Parallel,
        Vectorize,
        Unroll,
        Atomic,
        AllowRaceConditions,
        GpuBlocks,
        GpuThreads,
        GpuLanes,
        GpuTile,
        Gpu,
        GpuSingleThread,
        Hexagon,
        Partition,
        NeverPartition,
        NeverPartitionAll,
        AlwaysPartition,
        AlwaysPartitionAll,
        Host,
        SmeStreaming,
        StreamLoads,
        StreamStores,
        EagerInline,
        ComputeWith,
        Prefetch,
        Specialize,
        SpecializeFail,
        // Structural: these create a new Func, registered under `produces` for
        // later directives to schedule. Handled specially, not via the shared
        // Func/Stage dispatch.
        Rfactor,
        In,
        CloneIn,
        // Func-level (apply to the whole Func; `stage` must be 0):
        ComputeAt,
        ComputeRoot,
        ComputeInline,
        StoreAt,
        StoreRoot,
        Bound,
        AlignStorage,
        FoldStorage,
        ReorderStorage,
        StoreIn,
        Memoize,
        Async,
        RingBuffer,
        AlignBounds,
        AlignExtent,
        BoundExtent,
        BoundStorage,
        SetEstimate,
        SetEstimates,
        HoistStorage,
        HoistStorageRoot,
        TraceLoads,
        TraceStores,
        TraceRealizations,
        AddTraceTag,
        NoProfiling,
        CopyToHost,
        CopyToDevice,
    };

    /** What kind of scheduling call this is. */
    Kind kind = Kind::ComputeRoot;

    /** The Func this directive schedules, by name. */
    std::string func;

    /** Which definition: 0 == pure definition, N == update(N - 1). Must be 0
     * for Func-level kinds. */
    int stage = 0;

    /** Variable operands, in call order. */
    std::vector<VarSpec> vars;

    /** Expression operands (split/tile factors, bounds, extents), in call
     * order. */
    std::vector<Expr> exprs;

    /** Tail strategy for splits/tiles/vectorize/unroll/parallel/gpu_tile. */
    TailStrategy tail = TailStrategy::Auto;

    /** Device for the gpu_* directives. */
    DeviceAPI device = DeviceAPI::Default_GPU;

    /** Memory type for StoreIn. */
    MemoryType memory_type = MemoryType::Auto;

    /** General-purpose boolean flag: fold_forward for FoldStorage,
     * override_associativity_test for Atomic. */
    bool flag = false;

    /** For ComputeAt/StoreAt/HoistStorage: the loop level to compute/store at,
     * named by the target Func + var (+ optional stage). */
    std::string at_func;
    VarSpec at_var;
    int at_stage = -1;

    /** Loop partition policy for Partition. */
    Partition partition_policy = Partition::Auto;

    /** String payload for AddTraceTag (and, later, SpecializeFail). */
    std::string message;

    /** Other Funcs referenced by name (EagerInline, StreamLoads, Prefetch). */
    std::vector<std::string> ref_funcs;

    /** Loop-fusion alignment for ComputeWith: a single strategy, or one per
     * entry in `vars` when `var_aligns` is non-empty. */
    LoopAlignStrategy align = LoopAlignStrategy::Auto;
    std::vector<LoopAlignStrategy> var_aligns;

    /** Bounding strategy for Prefetch. */
    PrefetchBoundStrategy prefetch_strategy = PrefetchBoundStrategy::GuardWithIf;

    /** Enclosing specialize() conditions. A stage-level directive with a
     * non-empty path is applied within f.specialize(c0).specialize(c1)...;
     * empty means the base (unspecialized) stage. */
    std::vector<Expr> specialize_conditions;

    /** For the structural kinds (Rfactor/In/CloneIn), the name to register the
     * newly created Func under so later directives can schedule it. */
    std::string produces;

    // ------------------------------------------------------------------
    // Named constructors. `stage` defaults to the pure definition (0).
    // ------------------------------------------------------------------

    static ScheduleDirective split(std::string func, VarSpec old, VarSpec outer,
                                   VarSpec inner, Expr factor,
                                   TailStrategy tail = TailStrategy::Auto,
                                   int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Split;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = {std::move(old), std::move(outer), std::move(inner)};
        d.exprs = {std::move(factor)};
        d.tail = tail;
        return d;
    }

    static ScheduleDirective fuse(std::string func, VarSpec inner, VarSpec outer,
                                  VarSpec fused, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Fuse;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = {std::move(inner), std::move(outer), std::move(fused)};
        return d;
    }

    static ScheduleDirective rename(std::string func, VarSpec old, VarSpec renamed,
                                    int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Rename;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = {std::move(old), std::move(renamed)};
        return d;
    }

    static ScheduleDirective reorder(std::string func, std::vector<VarSpec> vars,
                                     int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Reorder;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = std::move(vars);
        return d;
    }

    /** A tile over N dimensions. `previous`, `outers`, and `inners` must each
     * have length N; `factors` gives the N tile sizes. */
    static ScheduleDirective tile(std::string func, std::vector<VarSpec> previous,
                                  std::vector<VarSpec> outers,
                                  std::vector<VarSpec> inners,
                                  std::vector<Expr> factors,
                                  TailStrategy tail = TailStrategy::Auto,
                                  int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Tile;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = std::move(previous);
        d.vars.insert(d.vars.end(), outers.begin(), outers.end());
        d.vars.insert(d.vars.end(), inners.begin(), inners.end());
        d.exprs = std::move(factors);
        d.tail = tail;
        return d;
    }

    static ScheduleDirective serial(std::string func, VarSpec var, int stage = 0) {
        return unary(Kind::Serial, std::move(func), std::move(var), stage);
    }

    static ScheduleDirective parallel(std::string func, VarSpec var, int stage = 0) {
        return unary(Kind::Parallel, std::move(func), std::move(var), stage);
    }

    static ScheduleDirective parallel(std::string func, VarSpec var, Expr task_size,
                                      TailStrategy tail = TailStrategy::Auto,
                                      int stage = 0) {
        ScheduleDirective d = unary(Kind::Parallel, std::move(func), std::move(var), stage);
        d.exprs = {std::move(task_size)};
        d.tail = tail;
        return d;
    }

    static ScheduleDirective vectorize(std::string func, VarSpec var, int stage = 0) {
        return unary(Kind::Vectorize, std::move(func), std::move(var), stage);
    }

    static ScheduleDirective vectorize(std::string func, VarSpec var, Expr factor,
                                       TailStrategy tail = TailStrategy::Auto,
                                       int stage = 0) {
        ScheduleDirective d = unary(Kind::Vectorize, std::move(func), std::move(var), stage);
        d.exprs = {std::move(factor)};
        d.tail = tail;
        return d;
    }

    static ScheduleDirective unroll(std::string func, VarSpec var, int stage = 0) {
        return unary(Kind::Unroll, std::move(func), std::move(var), stage);
    }

    static ScheduleDirective unroll(std::string func, VarSpec var, Expr factor,
                                    TailStrategy tail = TailStrategy::Auto,
                                    int stage = 0) {
        ScheduleDirective d = unary(Kind::Unroll, std::move(func), std::move(var), stage);
        d.exprs = {std::move(factor)};
        d.tail = tail;
        return d;
    }

    static ScheduleDirective atomic(std::string func, bool override_associativity_test = false,
                                    int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Atomic;
        d.func = std::move(func);
        d.stage = stage;
        d.flag = override_associativity_test;
        return d;
    }

    static ScheduleDirective allow_race_conditions(std::string func, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::AllowRaceConditions;
        d.func = std::move(func);
        d.stage = stage;
        return d;
    }

    static ScheduleDirective gpu_blocks(std::string func, std::vector<VarSpec> block_vars,
                                        DeviceAPI device = DeviceAPI::Default_GPU,
                                        int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::GpuBlocks;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = std::move(block_vars);
        d.device = device;
        return d;
    }

    static ScheduleDirective gpu_threads(std::string func, std::vector<VarSpec> thread_vars,
                                         DeviceAPI device = DeviceAPI::Default_GPU,
                                         int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::GpuThreads;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = std::move(thread_vars);
        d.device = device;
        return d;
    }

    static ScheduleDirective gpu_lanes(std::string func, VarSpec thread_x,
                                       DeviceAPI device = DeviceAPI::Default_GPU,
                                       int stage = 0) {
        ScheduleDirective d = unary(Kind::GpuLanes, std::move(func), std::move(thread_x), stage);
        d.device = device;
        return d;
    }

    /** A 1D gpu_tile: gpu_tile(x, bx, tx, x_size). */
    static ScheduleDirective gpu_tile(std::string func, VarSpec x, VarSpec bx,
                                      VarSpec tx, Expr x_size,
                                      TailStrategy tail = TailStrategy::Auto,
                                      DeviceAPI device = DeviceAPI::Default_GPU,
                                      int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::GpuTile;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = {std::move(x), std::move(bx), std::move(tx)};
        d.exprs = {std::move(x_size)};
        d.tail = tail;
        d.device = device;
        return d;
    }

    /** A 2D gpu_tile: gpu_tile(x, y, bx, by, tx, ty, x_size, y_size). */
    static ScheduleDirective gpu_tile(std::string func, VarSpec x, VarSpec y,
                                      VarSpec bx, VarSpec by, VarSpec tx, VarSpec ty,
                                      Expr x_size, Expr y_size,
                                      TailStrategy tail = TailStrategy::Auto,
                                      DeviceAPI device = DeviceAPI::Default_GPU,
                                      int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::GpuTile;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = {std::move(x), std::move(y), std::move(bx),
                  std::move(by), std::move(tx), std::move(ty)};
        d.exprs = {std::move(x_size), std::move(y_size)};
        d.tail = tail;
        d.device = device;
        return d;
    }

    static ScheduleDirective compute_at(std::string func, std::string at_func,
                                        VarSpec at_var, int at_stage = -1) {
        ScheduleDirective d;
        d.kind = Kind::ComputeAt;
        d.func = std::move(func);
        d.at_func = std::move(at_func);
        d.at_var = std::move(at_var);
        d.at_stage = at_stage;
        return d;
    }

    static ScheduleDirective compute_root(std::string func) {
        return nullary(Kind::ComputeRoot, std::move(func));
    }

    static ScheduleDirective compute_inline(std::string func) {
        return nullary(Kind::ComputeInline, std::move(func));
    }

    static ScheduleDirective store_at(std::string func, std::string at_func,
                                      VarSpec at_var, int at_stage = -1) {
        ScheduleDirective d;
        d.kind = Kind::StoreAt;
        d.func = std::move(func);
        d.at_func = std::move(at_func);
        d.at_var = std::move(at_var);
        d.at_stage = at_stage;
        return d;
    }

    static ScheduleDirective store_root(std::string func) {
        return nullary(Kind::StoreRoot, std::move(func));
    }

    static ScheduleDirective bound(std::string func, VarSpec var, Expr min, Expr extent) {
        ScheduleDirective d;
        d.kind = Kind::Bound;
        d.func = std::move(func);
        d.vars = {std::move(var)};
        d.exprs = {std::move(min), std::move(extent)};
        return d;
    }

    static ScheduleDirective align_storage(std::string func, VarSpec var, Expr alignment) {
        ScheduleDirective d;
        d.kind = Kind::AlignStorage;
        d.func = std::move(func);
        d.vars = {std::move(var)};
        d.exprs = {std::move(alignment)};
        return d;
    }

    static ScheduleDirective fold_storage(std::string func, VarSpec var, Expr extent,
                                          bool fold_forward = true) {
        ScheduleDirective d;
        d.kind = Kind::FoldStorage;
        d.func = std::move(func);
        d.vars = {std::move(var)};
        d.exprs = {std::move(extent)};
        d.flag = fold_forward;
        return d;
    }

    static ScheduleDirective reorder_storage(std::string func, std::vector<VarSpec> dims) {
        ScheduleDirective d;
        d.kind = Kind::ReorderStorage;
        d.func = std::move(func);
        d.vars = std::move(dims);
        return d;
    }

    static ScheduleDirective store_in(std::string func, MemoryType memory_type) {
        ScheduleDirective d;
        d.kind = Kind::StoreIn;
        d.func = std::move(func);
        d.memory_type = memory_type;
        return d;
    }

    static ScheduleDirective memoize(std::string func) {
        return nullary(Kind::Memoize, std::move(func));
    }

    static ScheduleDirective async(std::string func) {
        return nullary(Kind::Async, std::move(func));
    }

    static ScheduleDirective ring_buffer(std::string func, Expr extent) {
        ScheduleDirective d;
        d.kind = Kind::RingBuffer;
        d.func = std::move(func);
        d.exprs = {std::move(extent)};
        return d;
    }

    /** A combined gpu() over N dimensions: `block` and `thread` must have the
     * same length N. */
    static ScheduleDirective gpu(std::string func, std::vector<VarSpec> block,
                                 std::vector<VarSpec> thread,
                                 DeviceAPI device = DeviceAPI::Default_GPU, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Gpu;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = std::move(block);
        d.vars.insert(d.vars.end(), thread.begin(), thread.end());
        d.device = device;
        return d;
    }

    static ScheduleDirective gpu_single_thread(std::string func,
                                               DeviceAPI device = DeviceAPI::Default_GPU,
                                               int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::GpuSingleThread;
        d.func = std::move(func);
        d.stage = stage;
        d.device = device;
        return d;
    }

    static ScheduleDirective hexagon(std::string func, VarSpec x = {}, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Hexagon;
        d.func = std::move(func);
        d.stage = stage;
        if (!x.name.empty()) {
            d.vars = {std::move(x)};
        }
        return d;
    }

    static ScheduleDirective partition(std::string func, VarSpec var, Partition policy,
                                       int stage = 0) {
        ScheduleDirective d = unary(Kind::Partition, std::move(func), std::move(var), stage);
        d.partition_policy = policy;
        return d;
    }

    static ScheduleDirective never_partition(std::string func, std::vector<VarSpec> vars,
                                             int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::NeverPartition;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = std::move(vars);
        return d;
    }

    static ScheduleDirective always_partition(std::string func, std::vector<VarSpec> vars,
                                              int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::AlwaysPartition;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = std::move(vars);
        return d;
    }

    static ScheduleDirective never_partition_all(std::string func, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::NeverPartitionAll;
        d.func = std::move(func);
        d.stage = stage;
        return d;
    }

    static ScheduleDirective always_partition_all(std::string func, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::AlwaysPartitionAll;
        d.func = std::move(func);
        d.stage = stage;
        return d;
    }

    static ScheduleDirective host(std::string func, VarSpec x = {}, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Host;
        d.func = std::move(func);
        d.stage = stage;
        if (!x.name.empty()) {
            d.vars = {std::move(x)};
        }
        return d;
    }

    static ScheduleDirective sme_streaming(std::string func, VarSpec x = {}, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::SmeStreaming;
        d.func = std::move(func);
        d.stage = stage;
        if (!x.name.empty()) {
            d.vars = {std::move(x)};
        }
        return d;
    }

    static ScheduleDirective stream_loads(std::string func, std::vector<std::string> from = {},
                                          int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::StreamLoads;
        d.func = std::move(func);
        d.stage = stage;
        d.ref_funcs = std::move(from);
        return d;
    }

    static ScheduleDirective stream_stores(std::string func, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::StreamStores;
        d.func = std::move(func);
        d.stage = stage;
        return d;
    }

    static ScheduleDirective eager_inline(std::string func, std::vector<std::string> fs,
                                          int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::EagerInline;
        d.func = std::move(func);
        d.stage = stage;
        d.ref_funcs = std::move(fs);
        return d;
    }

    static ScheduleDirective align_bounds(std::string func, VarSpec var, Expr modulus,
                                          Expr remainder = 0) {
        ScheduleDirective d;
        d.kind = Kind::AlignBounds;
        d.func = std::move(func);
        d.vars = {std::move(var)};
        d.exprs = {std::move(modulus), std::move(remainder)};
        return d;
    }

    static ScheduleDirective align_extent(std::string func, VarSpec var, Expr modulus) {
        ScheduleDirective d;
        d.kind = Kind::AlignExtent;
        d.func = std::move(func);
        d.vars = {std::move(var)};
        d.exprs = {std::move(modulus)};
        return d;
    }

    static ScheduleDirective bound_extent(std::string func, VarSpec var, Expr extent) {
        ScheduleDirective d;
        d.kind = Kind::BoundExtent;
        d.func = std::move(func);
        d.vars = {std::move(var)};
        d.exprs = {std::move(extent)};
        return d;
    }

    static ScheduleDirective bound_storage(std::string func, VarSpec var, Expr bound) {
        ScheduleDirective d;
        d.kind = Kind::BoundStorage;
        d.func = std::move(func);
        d.vars = {std::move(var)};
        d.exprs = {std::move(bound)};
        return d;
    }

    static ScheduleDirective set_estimate(std::string func, VarSpec var, Expr min, Expr extent) {
        ScheduleDirective d;
        d.kind = Kind::SetEstimate;
        d.func = std::move(func);
        d.vars = {std::move(var)};
        d.exprs = {std::move(min), std::move(extent)};
        return d;
    }

    /** Positional estimates, one (min, extent) pair per pure dimension in order. */
    static ScheduleDirective set_estimates(std::string func,
                                           std::vector<std::pair<Expr, Expr>> estimates) {
        ScheduleDirective d;
        d.kind = Kind::SetEstimates;
        d.func = std::move(func);
        for (auto &e : estimates) {
            d.exprs.push_back(std::move(e.first));
            d.exprs.push_back(std::move(e.second));
        }
        return d;
    }

    static ScheduleDirective hoist_storage(std::string func, std::string at_func,
                                           VarSpec at_var, int at_stage = -1) {
        ScheduleDirective d;
        d.kind = Kind::HoistStorage;
        d.func = std::move(func);
        d.at_func = std::move(at_func);
        d.at_var = std::move(at_var);
        d.at_stage = at_stage;
        return d;
    }

    static ScheduleDirective hoist_storage_root(std::string func) {
        return nullary(Kind::HoistStorageRoot, std::move(func));
    }

    static ScheduleDirective trace_loads(std::string func) {
        return nullary(Kind::TraceLoads, std::move(func));
    }

    static ScheduleDirective trace_stores(std::string func) {
        return nullary(Kind::TraceStores, std::move(func));
    }

    static ScheduleDirective trace_realizations(std::string func) {
        return nullary(Kind::TraceRealizations, std::move(func));
    }

    static ScheduleDirective add_trace_tag(std::string func, std::string tag) {
        ScheduleDirective d;
        d.kind = Kind::AddTraceTag;
        d.func = std::move(func);
        d.message = std::move(tag);
        return d;
    }

    static ScheduleDirective no_profiling(std::string func) {
        return nullary(Kind::NoProfiling, std::move(func));
    }

    static ScheduleDirective copy_to_host(std::string func) {
        return nullary(Kind::CopyToHost, std::move(func));
    }

    static ScheduleDirective copy_to_device(std::string func,
                                            DeviceAPI device = DeviceAPI::Default_GPU) {
        ScheduleDirective d;
        d.kind = Kind::CopyToDevice;
        d.func = std::move(func);
        d.device = device;
        return d;
    }

    /** Fuse this stage's loops with stage `with_stage` of `with_func` down to
     * loop level `with_var`, using a single alignment strategy. */
    static ScheduleDirective compute_with(std::string func, std::string with_func,
                                          VarSpec with_var,
                                          LoopAlignStrategy align = LoopAlignStrategy::Auto,
                                          int with_stage = 0, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::ComputeWith;
        d.func = std::move(func);
        d.stage = stage;
        d.at_func = std::move(with_func);
        d.at_var = std::move(with_var);
        d.at_stage = with_stage;
        d.align = align;
        return d;
    }

    /** As above, but with a per-Var alignment strategy. */
    static ScheduleDirective compute_with(std::string func, std::string with_func,
                                          VarSpec with_var,
                                          std::vector<std::pair<VarSpec, LoopAlignStrategy>> aligns,
                                          int with_stage = 0, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::ComputeWith;
        d.func = std::move(func);
        d.stage = stage;
        d.at_func = std::move(with_func);
        d.at_var = std::move(with_var);
        d.at_stage = with_stage;
        for (auto &a : aligns) {
            d.vars.push_back(std::move(a.first));
            d.var_aligns.push_back(a.second);
        }
        return d;
    }

    /** Prefetch `prefetched` at loop `at`, indexing from `from` with `offset`. */
    static ScheduleDirective prefetch(std::string func, std::string prefetched, VarSpec at,
                                      VarSpec from, Expr offset = 1,
                                      PrefetchBoundStrategy strategy = PrefetchBoundStrategy::GuardWithIf,
                                      int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Prefetch;
        d.func = std::move(func);
        d.stage = stage;
        d.ref_funcs = {std::move(prefetched)};
        d.vars = {std::move(at), std::move(from)};
        d.exprs = {std::move(offset)};
        d.prefetch_strategy = strategy;
        return d;
    }

    /** Create a specialization of this stage for `condition`. Directives that
     * should apply inside it carry `condition` in their specialize_conditions
     * (use ScheduleEditor's fluent specialize() to record them). */
    static ScheduleDirective specialize(std::string func, Expr condition, int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::Specialize;
        d.func = std::move(func);
        d.stage = stage;
        d.exprs = {std::move(condition)};
        return d;
    }

    static ScheduleDirective specialize_fail(std::string func, std::string message,
                                             int stage = 0) {
        ScheduleDirective d;
        d.kind = Kind::SpecializeFail;
        d.func = std::move(func);
        d.stage = stage;
        d.message = std::move(message);
        return d;
    }

    /** rfactor an associative update into an intermediate Func registered as
     * `produces`. `preserved` pairs each kept RVar with a new pure Var. */
    static ScheduleDirective rfactor(std::string func,
                                     std::vector<std::pair<VarSpec, VarSpec>> preserved,
                                     std::string produces, int stage) {
        ScheduleDirective d;
        d.kind = Kind::Rfactor;
        d.func = std::move(func);
        d.stage = stage;
        d.produces = std::move(produces);
        for (auto &p : preserved) {
            d.vars.push_back(std::move(p.first));
            d.vars.push_back(std::move(p.second));
        }
        return d;
    }

    /** Wrap this Func in a new Func (registered as `produces`) between it and
     * its consumers `consumers` (empty means all consumers). */
    static ScheduleDirective in(std::string func, std::vector<std::string> consumers,
                                std::string produces) {
        ScheduleDirective d;
        d.kind = Kind::In;
        d.func = std::move(func);
        d.ref_funcs = std::move(consumers);
        d.produces = std::move(produces);
        return d;
    }

    static ScheduleDirective clone_in(std::string func, std::vector<std::string> consumers,
                                      std::string produces) {
        ScheduleDirective d;
        d.kind = Kind::CloneIn;
        d.func = std::move(func);
        d.ref_funcs = std::move(consumers);
        d.produces = std::move(produces);
        return d;
    }

    /** Whether this kind applies to a single stage (the pure definition or an
     * update) rather than to the whole Func -- i.e. it exists on both Func and
     * Stage and is dispatched through the shared scheduling methods. */
    bool is_stage_level() const {
        switch (kind) {
        case Kind::ComputeAt:
        case Kind::ComputeRoot:
        case Kind::ComputeInline:
        case Kind::StoreAt:
        case Kind::StoreRoot:
        case Kind::Bound:
        case Kind::AlignStorage:
        case Kind::FoldStorage:
        case Kind::ReorderStorage:
        case Kind::StoreIn:
        case Kind::Memoize:
        case Kind::Async:
        case Kind::RingBuffer:
        case Kind::AlignBounds:
        case Kind::AlignExtent:
        case Kind::BoundExtent:
        case Kind::BoundStorage:
        case Kind::SetEstimate:
        case Kind::SetEstimates:
        case Kind::HoistStorage:
        case Kind::HoistStorageRoot:
        case Kind::TraceLoads:
        case Kind::TraceStores:
        case Kind::TraceRealizations:
        case Kind::AddTraceTag:
        case Kind::NoProfiling:
        case Kind::CopyToHost:
        case Kind::CopyToDevice:
            return false;
        default:
            return true;
        }
    }

    /** Print this directive as a single C++ statement, e.g.
     * "blur_y.update(0).split(y, yo, yi, 8);". Expressions are printed using
     * Halide's IR printer. */
    std::string to_source() const;

private:
    static ScheduleDirective nullary(Kind kind, std::string func) {
        ScheduleDirective d;
        d.kind = kind;
        d.func = std::move(func);
        return d;
    }
    static ScheduleDirective unary(Kind kind, std::string func, VarSpec var, int stage) {
        ScheduleDirective d;
        d.kind = kind;
        d.func = std::move(func);
        d.stage = stage;
        d.vars = {std::move(var)};
        return d;
    }
};

/** An ordered list of scheduling directives -- the editable program a
 * ScheduleEditor builds and a ScheduleValidator checks. */
using ScheduleDirectives = std::vector<ScheduleDirective>;

namespace Internal {
/** The reference-counted state shared by a ScheduleEditor and the StageHandles
 * it hands out. Following the same IntrusivePtr-over-Contents pattern as Func,
 * LoopLevel, and FuncSchedule. */
struct ScheduleEditorContents {
    mutable RefCount ref_count;
    ScheduleDirectives directives;
    std::map<std::string, Func> funcs;
    std::function<std::vector<Func>()> rebuild;
};
}  // namespace Internal

class StageHandle;

/** An ordered, editable list of ScheduleDirectives that can be applied to a
 * pipeline. Use it to add, insert, remove, and reorder scheduling steps.
 *
 * ScheduleEditor is a lightweight handle over a reference-counted list of
 * directives, like Func or LoopLevel. Copies share the same underlying list,
 * and any StageHandle keeps that list alive for as long as it exists. */
class ScheduleEditor {
public:
    ScheduleEditor() = default;

    /** Register the Funcs this editor may schedule, along with everything
     * transitively reachable from them. Directives reference Funcs by name and
     * are resolved against this set at apply() time. */
    explicit ScheduleEditor(const std::vector<Func> &funcs);

    /** Convenience for a braced list of Funcs, e.g. ScheduleEditor({out}). */
    ScheduleEditor(std::initializer_list<Func> funcs)
        : ScheduleEditor(std::vector<Func>(funcs)) {
    }

    /** Register the output Funcs of a pipeline, plus everything reachable from
     * them. */
    explicit ScheduleEditor(const Pipeline &p);

    /** Supply a factory that rebuilds the unscheduled algorithm and returns its
     * output Func(s). Used by materialize() to apply the edited list to a fresh
     * pipeline, since an existing schedule cannot be undone. */
    explicit ScheduleEditor(std::function<std::vector<Func>()> rebuild) {
        contents->rebuild = std::move(rebuild);
    }

    /** Initialize the editor from an existing directive list (e.g. another
     * editor's directives() or the result of ScheduleAnalyzer::from_json()). No
     * Funcs are registered; apply() resolves the directives by name against the
     * Funcs you pass it. */
    explicit ScheduleEditor(const ScheduleDirectives &directives) {
        contents->directives = directives;
    }

    /** Register an additional Func (and its transitive dependencies), or replace
     * one of the same name. */
    void register_func(const Func &f);

    // ------------------------------------------------------------------
    // Editing the list. The directives are stored in order, so adding,
    // inserting, removing, and reordering are ordinary vector operations.
    // ------------------------------------------------------------------

    /** Append a directive; returns its index. */
    size_t append(ScheduleDirective d) {
        contents->directives.push_back(std::move(d));
        return contents->directives.size() - 1;
    }

    /** Insert a directive before position `index` (index == size() appends). */
    void insert(size_t index, ScheduleDirective d);

    /** Remove the directive at `index`. */
    void remove(size_t index);

    /** Replace the directive at `index`. */
    void replace(size_t index, ScheduleDirective d);

    /** Move the directive at `from` so that it sits at `to`, shifting the rest. */
    void move(size_t from, size_t to);

    /** Remove every directive matching a predicate; returns how many were
     * removed. */
    size_t remove_matching(const std::function<bool(const ScheduleDirective &)> &pred);

    /** Drop all directives (registered Funcs and factory are kept). */
    void clear() {
        contents->directives.clear();
    }

    // ------------------------------------------------------------------
    // Inspection.
    // ------------------------------------------------------------------

    size_t size() const {
        return contents->directives.size();
    }
    bool empty() const {
        return contents->directives.empty();
    }
    const ScheduleDirective &operator[](size_t i) const {
        return contents->directives[i];
    }
    ScheduleDirective &operator[](size_t i) {
        return contents->directives[i];
    }
    const ScheduleDirectives &directives() const {
        return contents->directives;
    }
    std::vector<ScheduleDirective>::const_iterator begin() const {
        return contents->directives.begin();
    }
    std::vector<ScheduleDirective>::const_iterator end() const {
        return contents->directives.end();
    }

    /** Indices of all directives targeting `func`, in order. */
    std::vector<size_t> find(const std::string &func) const;

    /** Indices of directives targeting stage `stage` of `func`, in order. */
    std::vector<size_t> find(const std::string &func, int stage) const;

    // ------------------------------------------------------------------
    // Fluent interface. Mirrors the usual scheduling calls, but records
    // directives instead of applying them.
    // ------------------------------------------------------------------

    /** Begin recording directives that append to the end of the list. */
    StageHandle schedule(const std::string &func, int stage = 0);

    /** Begin recording directives inserted starting at `index`. */
    StageHandle insert_at(size_t index, const std::string &func, int stage = 0);

    // ------------------------------------------------------------------
    // Materialize.
    // ------------------------------------------------------------------

    /** Replay the directives onto the given Funcs (resolved by name, added to
     * the editor's registered set for this call). */
    void apply(const std::vector<Func> &funcs) const;

    /** Replay onto the Funcs registered with the editor. */
    void apply() const;

    /** Replay onto the outputs (and reachable Funcs) of an existing pipeline. */
    void apply(const Pipeline &p) const;

    /** Rebuild a fresh, unscheduled pipeline via the factory, replay the
     * directives onto it, and return it. Requires the factory constructor. */
    Pipeline materialize() const;

private:
    Internal::IntrusivePtr<Internal::ScheduleEditorContents> contents{
        new Internal::ScheduleEditorContents};

    void apply_to(std::map<std::string, Func> &funcs) const;
};

/** Returned by ScheduleEditor::schedule and insert_at. Each method adds one
 * directive to the editor at an advancing position -- the end of the list for
 * schedule(), or a chosen index for insert_at() -- and returns *this so that
 * calls can be chained.
 *
 * A StageHandle holds its ScheduleEditor by value -- a cheap reference-counted
 * handle -- so it shares (and keeps alive) the same directive list and can be
 * stored or passed around freely. */
class StageHandle {
public:
    StageHandle(ScheduleEditor editor, std::string func, int stage, size_t pos)
        : editor_(std::move(editor)), func_(std::move(func)), stage_(stage), pos_(pos) {
    }

    StageHandle &split(VarSpec old, VarSpec outer, VarSpec inner, Expr factor,
                       TailStrategy tail = TailStrategy::Auto) {
        return add(ScheduleDirective::split(func_, std::move(old), std::move(outer),
                                            std::move(inner), std::move(factor), tail, stage_));
    }
    StageHandle &fuse(VarSpec inner, VarSpec outer, VarSpec fused) {
        return add(ScheduleDirective::fuse(func_, std::move(inner), std::move(outer),
                                           std::move(fused), stage_));
    }
    StageHandle &rename(VarSpec old, VarSpec renamed) {
        return add(ScheduleDirective::rename(func_, std::move(old), std::move(renamed), stage_));
    }
    StageHandle &reorder(std::vector<VarSpec> vars) {
        return add(ScheduleDirective::reorder(func_, std::move(vars), stage_));
    }
    StageHandle &tile(std::vector<VarSpec> previous, std::vector<VarSpec> outers,
                      std::vector<VarSpec> inners, std::vector<Expr> factors,
                      TailStrategy tail = TailStrategy::Auto) {
        return add(ScheduleDirective::tile(func_, std::move(previous), std::move(outers),
                                           std::move(inners), std::move(factors), tail, stage_));
    }
    StageHandle &serial(VarSpec var) {
        return add(ScheduleDirective::serial(func_, std::move(var), stage_));
    }
    StageHandle &parallel(VarSpec var) {
        return add(ScheduleDirective::parallel(func_, std::move(var), stage_));
    }
    StageHandle &parallel(VarSpec var, Expr task_size, TailStrategy tail = TailStrategy::Auto) {
        return add(ScheduleDirective::parallel(func_, std::move(var), std::move(task_size), tail, stage_));
    }
    StageHandle &vectorize(VarSpec var) {
        return add(ScheduleDirective::vectorize(func_, std::move(var), stage_));
    }
    StageHandle &vectorize(VarSpec var, Expr factor, TailStrategy tail = TailStrategy::Auto) {
        return add(ScheduleDirective::vectorize(func_, std::move(var), std::move(factor), tail, stage_));
    }
    StageHandle &unroll(VarSpec var) {
        return add(ScheduleDirective::unroll(func_, std::move(var), stage_));
    }
    StageHandle &unroll(VarSpec var, Expr factor, TailStrategy tail = TailStrategy::Auto) {
        return add(ScheduleDirective::unroll(func_, std::move(var), std::move(factor), tail, stage_));
    }
    StageHandle &atomic(bool override_associativity_test = false) {
        return add(ScheduleDirective::atomic(func_, override_associativity_test, stage_));
    }
    StageHandle &allow_race_conditions() {
        return add(ScheduleDirective::allow_race_conditions(func_, stage_));
    }
    StageHandle &gpu_blocks(std::vector<VarSpec> block_vars,
                            DeviceAPI device = DeviceAPI::Default_GPU) {
        return add(ScheduleDirective::gpu_blocks(func_, std::move(block_vars), device, stage_));
    }
    StageHandle &gpu_threads(std::vector<VarSpec> thread_vars,
                             DeviceAPI device = DeviceAPI::Default_GPU) {
        return add(ScheduleDirective::gpu_threads(func_, std::move(thread_vars), device, stage_));
    }
    StageHandle &gpu_lanes(VarSpec thread_x, DeviceAPI device = DeviceAPI::Default_GPU) {
        return add(ScheduleDirective::gpu_lanes(func_, std::move(thread_x), device, stage_));
    }
    StageHandle &gpu_tile(VarSpec x, VarSpec bx, VarSpec tx, Expr x_size,
                          TailStrategy tail = TailStrategy::Auto,
                          DeviceAPI device = DeviceAPI::Default_GPU) {
        return add(ScheduleDirective::gpu_tile(func_, std::move(x), std::move(bx), std::move(tx),
                                               std::move(x_size), tail, device, stage_));
    }
    StageHandle &gpu_tile(VarSpec x, VarSpec y, VarSpec bx, VarSpec by, VarSpec tx, VarSpec ty,
                          Expr x_size, Expr y_size, TailStrategy tail = TailStrategy::Auto,
                          DeviceAPI device = DeviceAPI::Default_GPU) {
        return add(ScheduleDirective::gpu_tile(func_, std::move(x), std::move(y), std::move(bx),
                                               std::move(by), std::move(tx), std::move(ty),
                                               std::move(x_size), std::move(y_size), tail, device, stage_));
    }

    // Func-level directives (only valid on the pure definition).
    StageHandle &compute_at(const std::string &at_func, VarSpec at_var, int at_stage = -1) {
        return add(ScheduleDirective::compute_at(func_, at_func, std::move(at_var), at_stage));
    }
    StageHandle &compute_root() {
        return add(ScheduleDirective::compute_root(func_));
    }
    StageHandle &compute_inline() {
        return add(ScheduleDirective::compute_inline(func_));
    }
    StageHandle &store_at(const std::string &at_func, VarSpec at_var, int at_stage = -1) {
        return add(ScheduleDirective::store_at(func_, at_func, std::move(at_var), at_stage));
    }
    StageHandle &store_root() {
        return add(ScheduleDirective::store_root(func_));
    }
    StageHandle &bound(VarSpec var, Expr min, Expr extent) {
        return add(ScheduleDirective::bound(func_, std::move(var), std::move(min), std::move(extent)));
    }
    StageHandle &align_storage(VarSpec var, Expr alignment) {
        return add(ScheduleDirective::align_storage(func_, std::move(var), std::move(alignment)));
    }
    StageHandle &fold_storage(VarSpec var, Expr extent, bool fold_forward = true) {
        return add(ScheduleDirective::fold_storage(func_, std::move(var), std::move(extent), fold_forward));
    }
    StageHandle &reorder_storage(std::vector<VarSpec> dims) {
        return add(ScheduleDirective::reorder_storage(func_, std::move(dims)));
    }
    StageHandle &store_in(MemoryType memory_type) {
        return add(ScheduleDirective::store_in(func_, memory_type));
    }
    StageHandle &memoize() {
        return add(ScheduleDirective::memoize(func_));
    }
    StageHandle &async() {
        return add(ScheduleDirective::async(func_));
    }
    StageHandle &ring_buffer(Expr extent) {
        return add(ScheduleDirective::ring_buffer(func_, std::move(extent)));
    }
    StageHandle &gpu(std::vector<VarSpec> block, std::vector<VarSpec> thread,
                     DeviceAPI device = DeviceAPI::Default_GPU) {
        return add(ScheduleDirective::gpu(func_, std::move(block), std::move(thread), device, stage_));
    }
    StageHandle &gpu_single_thread(DeviceAPI device = DeviceAPI::Default_GPU) {
        return add(ScheduleDirective::gpu_single_thread(func_, device, stage_));
    }
    StageHandle &hexagon(VarSpec x = {}) {
        return add(ScheduleDirective::hexagon(func_, std::move(x), stage_));
    }
    StageHandle &partition(VarSpec var, Partition policy) {
        return add(ScheduleDirective::partition(func_, std::move(var), policy, stage_));
    }
    StageHandle &never_partition(std::vector<VarSpec> vars) {
        return add(ScheduleDirective::never_partition(func_, std::move(vars), stage_));
    }
    StageHandle &always_partition(std::vector<VarSpec> vars) {
        return add(ScheduleDirective::always_partition(func_, std::move(vars), stage_));
    }
    StageHandle &never_partition_all() {
        return add(ScheduleDirective::never_partition_all(func_, stage_));
    }
    StageHandle &always_partition_all() {
        return add(ScheduleDirective::always_partition_all(func_, stage_));
    }
    StageHandle &host(VarSpec x = {}) {
        return add(ScheduleDirective::host(func_, std::move(x), stage_));
    }
    StageHandle &sme_streaming(VarSpec x = {}) {
        return add(ScheduleDirective::sme_streaming(func_, std::move(x), stage_));
    }
    StageHandle &stream_loads(std::vector<std::string> from = {}) {
        return add(ScheduleDirective::stream_loads(func_, std::move(from), stage_));
    }
    StageHandle &stream_stores() {
        return add(ScheduleDirective::stream_stores(func_, stage_));
    }
    StageHandle &eager_inline(std::vector<std::string> fs) {
        return add(ScheduleDirective::eager_inline(func_, std::move(fs), stage_));
    }
    StageHandle &align_bounds(VarSpec var, Expr modulus, Expr remainder = 0) {
        return add(ScheduleDirective::align_bounds(func_, std::move(var), std::move(modulus), std::move(remainder)));
    }
    StageHandle &align_extent(VarSpec var, Expr modulus) {
        return add(ScheduleDirective::align_extent(func_, std::move(var), std::move(modulus)));
    }
    StageHandle &bound_extent(VarSpec var, Expr extent) {
        return add(ScheduleDirective::bound_extent(func_, std::move(var), std::move(extent)));
    }
    StageHandle &bound_storage(VarSpec var, Expr bound) {
        return add(ScheduleDirective::bound_storage(func_, std::move(var), std::move(bound)));
    }
    StageHandle &set_estimate(VarSpec var, Expr min, Expr extent) {
        return add(ScheduleDirective::set_estimate(func_, std::move(var), std::move(min), std::move(extent)));
    }
    StageHandle &set_estimates(std::vector<std::pair<Expr, Expr>> estimates) {
        return add(ScheduleDirective::set_estimates(func_, std::move(estimates)));
    }
    StageHandle &hoist_storage(const std::string &at_func, VarSpec at_var, int at_stage = -1) {
        return add(ScheduleDirective::hoist_storage(func_, at_func, std::move(at_var), at_stage));
    }
    StageHandle &hoist_storage_root() {
        return add(ScheduleDirective::hoist_storage_root(func_));
    }
    StageHandle &trace_loads() {
        return add(ScheduleDirective::trace_loads(func_));
    }
    StageHandle &trace_stores() {
        return add(ScheduleDirective::trace_stores(func_));
    }
    StageHandle &trace_realizations() {
        return add(ScheduleDirective::trace_realizations(func_));
    }
    StageHandle &add_trace_tag(std::string tag) {
        return add(ScheduleDirective::add_trace_tag(func_, std::move(tag)));
    }
    StageHandle &no_profiling() {
        return add(ScheduleDirective::no_profiling(func_));
    }
    StageHandle &copy_to_host() {
        return add(ScheduleDirective::copy_to_host(func_));
    }
    StageHandle &copy_to_device(DeviceAPI device = DeviceAPI::Default_GPU) {
        return add(ScheduleDirective::copy_to_device(func_, device));
    }
    StageHandle &compute_with(const std::string &with_func, VarSpec with_var,
                              LoopAlignStrategy align = LoopAlignStrategy::Auto,
                              int with_stage = 0) {
        return add(ScheduleDirective::compute_with(func_, with_func, std::move(with_var),
                                                   align, with_stage, stage_));
    }
    StageHandle &compute_with(const std::string &with_func, VarSpec with_var,
                              std::vector<std::pair<VarSpec, LoopAlignStrategy>> aligns,
                              int with_stage = 0) {
        return add(ScheduleDirective::compute_with(func_, with_func, std::move(with_var),
                                                   std::move(aligns), with_stage, stage_));
    }
    StageHandle &prefetch(const std::string &prefetched, VarSpec at, VarSpec from, Expr offset = 1,
                          PrefetchBoundStrategy strategy = PrefetchBoundStrategy::GuardWithIf) {
        return add(ScheduleDirective::prefetch(func_, prefetched, std::move(at), std::move(from),
                                               std::move(offset), strategy, stage_));
    }
    StageHandle &specialize_fail(std::string message) {
        return add(ScheduleDirective::specialize_fail(func_, std::move(message), stage_));
    }

    /** Create a specialization for `condition` and return a handle whose
     * subsequent calls are recorded inside it. */
    StageHandle specialize(Expr condition) {
        add(ScheduleDirective::specialize(func_, condition, stage_));
        StageHandle h(editor_, func_, stage_, pos_);
        h.specialize_path_ = specialize_path_;
        h.specialize_path_.push_back(std::move(condition));
        return h;
    }

    /** rfactor this update into an intermediate named `produces`, and return a
     * handle for scheduling that intermediate. */
    StageHandle rfactor(std::vector<std::pair<VarSpec, VarSpec>> preserved, std::string produces) {
        add(ScheduleDirective::rfactor(func_, std::move(preserved), produces, stage_));
        return StageHandle(editor_, std::move(produces), 0, pos_);
    }

    /** Wrap this Func for `consumers` (empty = all), returning a handle for the
     * new wrapper named `produces`. */
    StageHandle in(std::vector<std::string> consumers, std::string produces) {
        add(ScheduleDirective::in(func_, std::move(consumers), produces));
        return StageHandle(editor_, std::move(produces), 0, pos_);
    }
    StageHandle clone_in(std::vector<std::string> consumers, std::string produces) {
        add(ScheduleDirective::clone_in(func_, std::move(consumers), produces));
        return StageHandle(editor_, std::move(produces), 0, pos_);
    }

    /** Record subsequent calls on update stage `stage` of the same Func. */
    StageHandle update(int stage = 0) {
        StageHandle h(editor_, func_, stage + 1, pos_);
        h.specialize_path_ = specialize_path_;
        return h;
    }

    /** The editor this handle records into. */
    const ScheduleEditor &editor() const {
        return editor_;
    }

private:
    ScheduleEditor editor_;
    std::string func_;
    int stage_;
    size_t pos_;
    std::vector<Expr> specialize_path_;

    StageHandle &add(ScheduleDirective d) {
        d.specialize_conditions = specialize_path_;
        editor_.insert(pos_++, std::move(d));
        return *this;
    }
};

inline StageHandle ScheduleEditor::schedule(const std::string &func, int stage) {
    return StageHandle(*this, func, stage, contents->directives.size());
}

inline StageHandle ScheduleEditor::insert_at(size_t index, const std::string &func, int stage) {
    return StageHandle(*this, func, stage, index);
}

}  // namespace Halide

#endif  // HALIDE_SCHEDULE_EDITOR_H
