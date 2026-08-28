#include "ScheduleEditor.h"

#include <algorithm>
#include <sstream>

#include "Error.h"
#include "FindCalls.h"
#include "Function.h"
#include "IR.h"
#include "IRPrinter.h"

namespace Halide {

namespace {

// Halide uniquifies Func names when the same name is used more than once in a
// process (e.g. "blur_x" becomes "blur_x$2" the second time an algorithm is
// built). Directives are written against the logical name the user gave a Func,
// so we key the resolution map by that base name -- the substring before the
// uniquifier's '$' -- which stays stable every time the factory rebuilds.
std::string base_name(const std::string &name) {
    size_t dollar = name.find('$');
    return dollar == std::string::npos ? name : name.substr(0, dollar);
}

// Add `f` and every Func transitively called by it to `out`, keyed by base
// name. Scheduling any of the returned handles mutates the shared Function
// contents, so a Pipeline built from the same Funcs reflects the schedule.
void collect(const Func &f, std::map<std::string, Func> &out) {
    Internal::Function fn = f.function();
    out[base_name(fn.name())] = f;
    for (const auto &kv : Internal::find_transitive_calls(fn)) {
        out.emplace(base_name(kv.first), Func(kv.second));
    }
}

Func lookup(const std::map<std::string, Func> &funcs, const std::string &name) {
    auto it = funcs.find(base_name(name));
    if (it == funcs.end()) {
        std::ostringstream available;
        for (const auto &kv : funcs) {
            available << " " << kv.first;
        }
        user_error << "ScheduleEditor: no Func named '" << name
                   << "' has been registered with the editor. Registered Funcs:"
                   << available.str();
    }
    return it->second;
}

LoopLevel loop_level(const std::map<std::string, Func> &funcs,
                     const ScheduleDirective &d) {
    Func at = lookup(funcs, d.at_func);
    return LoopLevel(at, d.at_var.realize(), d.at_stage);
}

std::vector<Func> resolve_funcs(const std::map<std::string, Func> &funcs,
                                const std::vector<std::string> &names) {
    std::vector<Func> result;
    result.reserve(names.size());
    for (const std::string &n : names) {
        result.push_back(lookup(funcs, n));
    }
    return result;
}

// Stage-level dispatch. Templated because Func and Stage expose the same
// scheduling method names; instantiated for both.
template<typename T>
void apply_stage_op(T &t, const ScheduleDirective &d,
                    const std::map<std::string, Func> &funcs) {
    using K = ScheduleDirective::Kind;
    auto v = [&](size_t i) { return d.vars.at(i).realize(); };
    auto realize_all = [&]() {
        std::vector<VarOrRVar> out;
        out.reserve(d.vars.size());
        for (const VarSpec &s : d.vars) {
            out.push_back(s.realize());
        }
        return out;
    };
    switch (d.kind) {
    case K::Split:
        t.split(v(0), v(1), v(2), d.exprs.at(0), d.tail);
        break;
    case K::Fuse:
        t.fuse(v(0), v(1), v(2));
        break;
    case K::Rename:
        t.rename(v(0), v(1));
        break;
    case K::Reorder:
        t.reorder(realize_all());
        break;
    case K::Tile: {
        size_t n = d.exprs.size();
        std::vector<VarOrRVar> previous, outers, inners;
        for (size_t i = 0; i < n; i++) {
            previous.push_back(d.vars.at(i).realize());
        }
        for (size_t i = 0; i < n; i++) {
            outers.push_back(d.vars.at(n + i).realize());
        }
        for (size_t i = 0; i < n; i++) {
            inners.push_back(d.vars.at(2 * n + i).realize());
        }
        t.tile(previous, outers, inners, d.exprs, d.tail);
        break;
    }
    case K::Serial:
        t.serial(v(0));
        break;
    case K::Parallel:
        if (d.exprs.empty()) {
            t.parallel(v(0));
        } else {
            t.parallel(v(0), d.exprs.at(0), d.tail);
        }
        break;
    case K::Vectorize:
        if (d.exprs.empty()) {
            t.vectorize(v(0));
        } else {
            t.vectorize(v(0), d.exprs.at(0), d.tail);
        }
        break;
    case K::Unroll:
        if (d.exprs.empty()) {
            t.unroll(v(0));
        } else {
            t.unroll(v(0), d.exprs.at(0), d.tail);
        }
        break;
    case K::Atomic:
        t.atomic(d.flag);
        break;
    case K::AllowRaceConditions:
        t.allow_race_conditions();
        break;
    case K::GpuBlocks:
        if (d.vars.size() == 1) {
            t.gpu_blocks(v(0), d.device);
        } else if (d.vars.size() == 2) {
            t.gpu_blocks(v(0), v(1), d.device);
        } else {
            t.gpu_blocks(v(0), v(1), v(2), d.device);
        }
        break;
    case K::GpuThreads:
        if (d.vars.size() == 1) {
            t.gpu_threads(v(0), d.device);
        } else if (d.vars.size() == 2) {
            t.gpu_threads(v(0), v(1), d.device);
        } else {
            t.gpu_threads(v(0), v(1), v(2), d.device);
        }
        break;
    case K::GpuLanes:
        t.gpu_lanes(v(0), d.device);
        break;
    case K::GpuTile:
        if (d.exprs.size() == 1) {
            t.gpu_tile(v(0), v(1), v(2), d.exprs.at(0), d.tail, d.device);
        } else {
            t.gpu_tile(v(0), v(1), v(2), v(3), v(4), v(5),
                       d.exprs.at(0), d.exprs.at(1), d.tail, d.device);
        }
        break;
    case K::Gpu: {
        size_t n = d.vars.size() / 2;
        if (n == 1) {
            t.gpu(v(0), v(1), d.device);
        } else if (n == 2) {
            t.gpu(v(0), v(1), v(2), v(3), d.device);
        } else {
            t.gpu(v(0), v(1), v(2), v(3), v(4), v(5), d.device);
        }
        break;
    }
    case K::GpuSingleThread:
        t.gpu_single_thread(d.device);
        break;
    case K::Hexagon:
        if (d.vars.empty()) {
            t.hexagon();
        } else {
            t.hexagon(v(0));
        }
        break;
    case K::Partition:
        t.partition(v(0), d.partition_policy);
        break;
    case K::NeverPartition:
        t.never_partition(realize_all());
        break;
    case K::AlwaysPartition:
        t.always_partition(realize_all());
        break;
    case K::NeverPartitionAll:
        t.never_partition_all();
        break;
    case K::AlwaysPartitionAll:
        t.always_partition_all();
        break;
    case K::Host:
        if (d.vars.empty()) {
            t.host();
        } else {
            t.host(v(0));
        }
        break;
    case K::SmeStreaming:
        if (d.vars.empty()) {
            t.sme_streaming();
        } else {
            t.sme_streaming(v(0));
        }
        break;
    case K::StreamLoads:
        if (d.ref_funcs.empty()) {
            t.stream_loads();
        } else {
            t.stream_loads(resolve_funcs(funcs, d.ref_funcs));
        }
        break;
    case K::StreamStores:
        t.stream_stores();
        break;
    case K::EagerInline:
        t.eager_inline(resolve_funcs(funcs, d.ref_funcs));
        break;
    case K::ComputeWith: {
        LoopLevel ll = loop_level(funcs, d);
        if (d.var_aligns.empty()) {
            t.compute_with(ll, d.align);
        } else {
            std::vector<std::pair<VarOrRVar, LoopAlignStrategy>> aligns;
            for (size_t i = 0; i < d.var_aligns.size(); i++) {
                aligns.emplace_back(d.vars.at(i).realize(), d.var_aligns[i]);
            }
            t.compute_with(ll, aligns);
        }
        break;
    }
    case K::Prefetch:
        t.prefetch(lookup(funcs, d.ref_funcs.at(0)), v(0), v(1), d.exprs.at(0),
                   d.prefetch_strategy);
        break;
    case K::Specialize:
        t.specialize(d.exprs.at(0));
        break;
    case K::SpecializeFail:
        t.specialize_fail(d.message);
        break;
    default:
        internal_error << "ScheduleEditor: not a stage-level directive";
    }
}

void apply_func_op(const std::map<std::string, Func> &funcs, Func &f,
                   const ScheduleDirective &d) {
    using K = ScheduleDirective::Kind;
    switch (d.kind) {
    case K::ComputeAt:
        f.compute_at(loop_level(funcs, d));
        break;
    case K::ComputeRoot:
        f.compute_root();
        break;
    case K::ComputeInline:
        f.compute_inline();
        break;
    case K::StoreAt:
        f.store_at(loop_level(funcs, d));
        break;
    case K::StoreRoot:
        f.store_root();
        break;
    case K::Bound:
        f.bound(Var(d.vars.at(0).name), d.exprs.at(0), d.exprs.at(1));
        break;
    case K::AlignStorage:
        f.align_storage(Var(d.vars.at(0).name), d.exprs.at(0));
        break;
    case K::FoldStorage:
        f.fold_storage(Var(d.vars.at(0).name), d.exprs.at(0), d.flag);
        break;
    case K::ReorderStorage: {
        std::vector<Var> dims;
        for (const VarSpec &s : d.vars) {
            dims.emplace_back(s.name);
        }
        f.reorder_storage(dims);
        break;
    }
    case K::StoreIn:
        f.store_in(d.memory_type);
        break;
    case K::Memoize:
        f.memoize();
        break;
    case K::Async:
        f.async();
        break;
    case K::RingBuffer:
        f.ring_buffer(d.exprs.at(0));
        break;
    case K::AlignBounds:
        f.align_bounds(Var(d.vars.at(0).name), d.exprs.at(0), d.exprs.at(1));
        break;
    case K::AlignExtent:
        f.align_extent(Var(d.vars.at(0).name), d.exprs.at(0));
        break;
    case K::BoundExtent:
        f.bound_extent(Var(d.vars.at(0).name), d.exprs.at(0));
        break;
    case K::BoundStorage:
        f.bound_storage(Var(d.vars.at(0).name), d.exprs.at(0));
        break;
    case K::SetEstimate:
        f.set_estimate(Var(d.vars.at(0).name), d.exprs.at(0), d.exprs.at(1));
        break;
    case K::SetEstimates: {
        Region estimates;
        for (size_t i = 0; i + 1 < d.exprs.size(); i += 2) {
            estimates.emplace_back(d.exprs[i], d.exprs[i + 1]);
        }
        f.set_estimates(estimates);
        break;
    }
    case K::HoistStorage:
        f.hoist_storage(loop_level(funcs, d));
        break;
    case K::HoistStorageRoot:
        f.hoist_storage_root();
        break;
    case K::TraceLoads:
        f.trace_loads();
        break;
    case K::TraceStores:
        f.trace_stores();
        break;
    case K::TraceRealizations:
        f.trace_realizations();
        break;
    case K::AddTraceTag:
        f.add_trace_tag(d.message);
        break;
    case K::NoProfiling:
        f.no_profiling();
        break;
    case K::CopyToHost:
        f.copy_to_host();
        break;
    case K::CopyToDevice:
        f.copy_to_device(d.device);
        break;
    default:
        internal_error << "ScheduleEditor: not a Func-level directive";
    }
}

// Reach the stage a stage-level directive targets, descending into any
// specialize() scopes it was recorded inside.
template<typename ApplyBase, typename ApplyStage>
void with_target_stage(Func &f, const ScheduleDirective &d,
                       const ApplyBase &apply_base, const ApplyStage &apply_stage) {
    if (d.specialize_conditions.empty()) {
        if (d.stage == 0) {
            apply_base(f);
        } else {
            Stage s = f.update(d.stage - 1);
            apply_stage(s);
        }
        return;
    }
    Stage s = (d.stage == 0) ? f.specialize(d.specialize_conditions.front()) : f.update(d.stage - 1).specialize(d.specialize_conditions.front());
    for (size_t i = 1; i < d.specialize_conditions.size(); i++) {
        s = s.specialize(d.specialize_conditions[i]);
    }
    apply_stage(s);
}

void apply_directive(std::map<std::string, Func> &funcs,
                     const ScheduleDirective &d) {
    using K = ScheduleDirective::Kind;
    Func f = lookup(funcs, d.func);

    // Structural directives create a new Func and register it under `produces`
    // so later directives can schedule it.
    switch (d.kind) {
    case K::Rfactor: {
        user_assert(d.stage > 0)
            << "ScheduleEditor: rfactor requires an update stage (stage > 0).";
        std::vector<std::pair<RVar, Var>> preserved;
        for (size_t i = 0; i + 1 < d.vars.size(); i += 2) {
            preserved.emplace_back(RVar(d.vars[i].name), Var(d.vars[i + 1].name));
        }
        funcs[d.produces] = f.update(d.stage - 1).rfactor(preserved);
        return;
    }
    case K::In: {
        Func w;
        if (d.ref_funcs.empty()) {
            w = f.in();
        } else if (d.ref_funcs.size() == 1) {
            w = f.in(lookup(funcs, d.ref_funcs[0]));
        } else {
            w = f.in(resolve_funcs(funcs, d.ref_funcs));
        }
        funcs[d.produces] = w;
        return;
    }
    case K::CloneIn: {
        user_assert(!d.ref_funcs.empty())
            << "ScheduleEditor: clone_in requires at least one consumer Func.";
        Func w = (d.ref_funcs.size() == 1) ? f.clone_in(lookup(funcs, d.ref_funcs[0])) : f.clone_in(resolve_funcs(funcs, d.ref_funcs));
        funcs[d.produces] = w;
        return;
    }
    default:
        break;
    }

    if (d.is_stage_level()) {
        with_target_stage(
            f, d,
            [&](Func &base) { apply_stage_op(base, d, funcs); },
            [&](Stage &s) { apply_stage_op(s, d, funcs); });
    } else {
        user_assert(d.stage == 0)
            << "ScheduleEditor: Func-level directive on '" << d.func
            << "' must target stage 0.";
        user_assert(d.specialize_conditions.empty())
            << "ScheduleEditor: Func-level directive on '" << d.func
            << "' cannot be applied inside a specialize() scope.";
        apply_func_op(funcs, f, d);
    }
}

}  // namespace

namespace Internal {
template<>
RefCount &ref_count<ScheduleEditorContents>(const ScheduleEditorContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<ScheduleEditorContents>(const ScheduleEditorContents *p) {
    delete p;
}
}  // namespace Internal

ScheduleEditor::ScheduleEditor(const std::vector<Func> &funcs) {
    for (const Func &f : funcs) {
        collect(f, contents->funcs);
    }
}

ScheduleEditor::ScheduleEditor(const Pipeline &p) {
    for (const Func &f : p.outputs()) {
        collect(f, contents->funcs);
    }
}

void ScheduleEditor::register_func(const Func &f) {
    collect(f, contents->funcs);
}

void ScheduleEditor::insert(size_t index, ScheduleDirective d) {
    user_assert(index <= contents->directives.size())
        << "ScheduleEditor::insert index " << index << " out of range (size "
        << contents->directives.size() << ")";
    contents->directives.insert(contents->directives.begin() + index, std::move(d));
}

void ScheduleEditor::remove(size_t index) {
    user_assert(index < contents->directives.size())
        << "ScheduleEditor::remove index " << index << " out of range (size "
        << contents->directives.size() << ")";
    contents->directives.erase(contents->directives.begin() + index);
}

void ScheduleEditor::replace(size_t index, ScheduleDirective d) {
    user_assert(index < contents->directives.size())
        << "ScheduleEditor::replace index " << index << " out of range (size "
        << contents->directives.size() << ")";
    contents->directives[index] = std::move(d);
}

void ScheduleEditor::move(size_t from, size_t to) {
    user_assert(from < contents->directives.size() && to < contents->directives.size())
        << "ScheduleEditor::move index out of range (size " << contents->directives.size() << ")";
    if (from == to) {
        return;
    }
    ScheduleDirective d = std::move(contents->directives[from]);
    contents->directives.erase(contents->directives.begin() + from);
    contents->directives.insert(contents->directives.begin() + to, std::move(d));
}

size_t ScheduleEditor::remove_matching(
    const std::function<bool(const ScheduleDirective &)> &pred) {
    size_t before = contents->directives.size();
    contents->directives.erase(std::remove_if(contents->directives.begin(), contents->directives.end(),
                                              [&](const ScheduleDirective &d) { return pred(d); }),
                               contents->directives.end());
    return before - contents->directives.size();
}

std::vector<size_t> ScheduleEditor::find(const std::string &func) const {
    std::vector<size_t> result;
    for (size_t i = 0; i < contents->directives.size(); i++) {
        if (contents->directives[i].func == func) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<size_t> ScheduleEditor::find(const std::string &func, int stage) const {
    std::vector<size_t> result;
    for (size_t i : find(func)) {
        if (contents->directives[i].stage == stage) {
            result.push_back(i);
        }
    }
    return result;
}

void ScheduleEditor::apply_to(std::map<std::string, Func> &funcs) const {
    for (const ScheduleDirective &d : contents->directives) {
        apply_directive(funcs, d);
    }
}

void ScheduleEditor::apply(const std::vector<Func> &funcs) const {
    std::map<std::string, Func> resolved = contents->funcs;
    for (const Func &f : funcs) {
        collect(f, resolved);
    }
    apply_to(resolved);
}

void ScheduleEditor::apply() const {
    std::map<std::string, Func> resolved = contents->funcs;
    apply_to(resolved);
}

void ScheduleEditor::apply(const Pipeline &p) const {
    std::map<std::string, Func> resolved = contents->funcs;
    for (const Func &f : p.outputs()) {
        collect(f, resolved);
    }
    apply_to(resolved);
}

Pipeline ScheduleEditor::materialize() const {
    user_assert(contents->rebuild)
        << "ScheduleEditor::materialize requires the factory constructor "
           "(ScheduleEditor(std::function<std::vector<Func>()>)).";
    std::vector<Func> outputs = contents->rebuild();
    std::map<std::string, Func> resolved;
    for (const Func &f : outputs) {
        collect(f, resolved);
    }
    apply_to(resolved);
    return Pipeline(outputs);
}

std::string ScheduleDirective::to_source() const {
    std::ostringstream os;
    auto expr_str = [](const Expr &e) {
        std::ostringstream s;
        s << e;
        return s.str();
    };
    auto var_list = [&](std::ostream &s, size_t begin, size_t end) {
        for (size_t i = begin; i < end; i++) {
            s << (i > begin ? ", " : "") << vars[i].name;
        }
    };
    auto func_list = [&](std::ostream &s) {
        for (size_t i = 0; i < ref_funcs.size(); i++) {
            s << (i ? ", " : "") << ref_funcs[i];
        }
    };

    os << func;
    if (stage != 0) {
        os << ".update(" << (stage - 1) << ")";
    }
    for (const Expr &c : specialize_conditions) {
        os << ".specialize(" << expr_str(c) << ")";
    }
    os << ".";

    using K = Kind;
    switch (kind) {
    case K::Split:
        os << "split(" << vars[0].name << ", " << vars[1].name << ", " << vars[2].name
           << ", " << expr_str(exprs[0]) << ")";
        break;
    case K::Fuse:
        os << "fuse(" << vars[0].name << ", " << vars[1].name << ", " << vars[2].name << ")";
        break;
    case K::Rename:
        os << "rename(" << vars[0].name << ", " << vars[1].name << ")";
        break;
    case K::Reorder:
        os << "reorder(";
        var_list(os, 0, vars.size());
        os << ")";
        break;
    case K::Tile: {
        size_t n = exprs.size();
        os << "tile({";
        var_list(os, 0, n);
        os << "}, {";
        var_list(os, n, 2 * n);
        os << "}, {";
        var_list(os, 2 * n, 3 * n);
        os << "}, {";
        for (size_t i = 0; i < n; i++) {
            os << (i ? ", " : "") << expr_str(exprs[i]);
        }
        os << "})";
        break;
    }
    case K::Serial:
        os << "serial(" << vars[0].name << ")";
        break;
    case K::Parallel:
        os << "parallel(" << vars[0].name;
        if (!exprs.empty()) {
            os << ", " << expr_str(exprs[0]);
        }
        os << ")";
        break;
    case K::Vectorize:
        os << "vectorize(" << vars[0].name;
        if (!exprs.empty()) {
            os << ", " << expr_str(exprs[0]);
        }
        os << ")";
        break;
    case K::Unroll:
        os << "unroll(" << vars[0].name;
        if (!exprs.empty()) {
            os << ", " << expr_str(exprs[0]);
        }
        os << ")";
        break;
    case K::Atomic:
        os << "atomic(" << (flag ? "true" : "") << ")";
        break;
    case K::AllowRaceConditions:
        os << "allow_race_conditions()";
        break;
    case K::GpuBlocks:
        os << "gpu_blocks(";
        var_list(os, 0, vars.size());
        os << ")";
        break;
    case K::GpuThreads:
        os << "gpu_threads(";
        var_list(os, 0, vars.size());
        os << ")";
        break;
    case K::GpuLanes:
        os << "gpu_lanes(" << vars[0].name << ")";
        break;
    case K::GpuTile:
        os << "gpu_tile(";
        var_list(os, 0, vars.size());
        for (const Expr &e : exprs) {
            os << ", " << expr_str(e);
        }
        os << ")";
        break;
    case K::ComputeAt:
        os << "compute_at(" << at_func << ", " << at_var.name << ")";
        break;
    case K::ComputeRoot:
        os << "compute_root()";
        break;
    case K::ComputeInline:
        os << "compute_inline()";
        break;
    case K::StoreAt:
        os << "store_at(" << at_func << ", " << at_var.name << ")";
        break;
    case K::StoreRoot:
        os << "store_root()";
        break;
    case K::Bound:
        os << "bound(" << vars[0].name << ", " << expr_str(exprs[0]) << ", "
           << expr_str(exprs[1]) << ")";
        break;
    case K::AlignStorage:
        os << "align_storage(" << vars[0].name << ", " << expr_str(exprs[0]) << ")";
        break;
    case K::FoldStorage:
        os << "fold_storage(" << vars[0].name << ", " << expr_str(exprs[0])
           << (flag ? "" : ", false") << ")";
        break;
    case K::ReorderStorage:
        os << "reorder_storage(";
        var_list(os, 0, vars.size());
        os << ")";
        break;
    case K::StoreIn:
        os << "store_in(MemoryType(" << (int)memory_type << "))";
        break;
    case K::Memoize:
        os << "memoize()";
        break;
    case K::Async:
        os << "async()";
        break;
    case K::RingBuffer:
        os << "ring_buffer(" << expr_str(exprs[0]) << ")";
        break;
    case K::Gpu:
        os << "gpu(";
        var_list(os, 0, vars.size());
        os << ")";
        break;
    case K::GpuSingleThread:
        os << "gpu_single_thread()";
        break;
    case K::Hexagon:
        os << "hexagon(" << (vars.empty() ? "" : vars[0].name) << ")";
        break;
    case K::Partition:
        os << "partition(" << vars[0].name << ", Partition(" << (int)partition_policy << "))";
        break;
    case K::NeverPartition:
        os << "never_partition(";
        var_list(os, 0, vars.size());
        os << ")";
        break;
    case K::AlwaysPartition:
        os << "always_partition(";
        var_list(os, 0, vars.size());
        os << ")";
        break;
    case K::NeverPartitionAll:
        os << "never_partition_all()";
        break;
    case K::AlwaysPartitionAll:
        os << "always_partition_all()";
        break;
    case K::Host:
        os << "host(" << (vars.empty() ? "" : vars[0].name) << ")";
        break;
    case K::SmeStreaming:
        os << "sme_streaming(" << (vars.empty() ? "" : vars[0].name) << ")";
        break;
    case K::StreamLoads:
        os << "stream_loads(";
        func_list(os);
        os << ")";
        break;
    case K::StreamStores:
        os << "stream_stores()";
        break;
    case K::EagerInline:
        os << "eager_inline(";
        func_list(os);
        os << ")";
        break;
    case K::AlignBounds:
        os << "align_bounds(" << vars[0].name << ", " << expr_str(exprs[0]) << ", "
           << expr_str(exprs[1]) << ")";
        break;
    case K::AlignExtent:
        os << "align_extent(" << vars[0].name << ", " << expr_str(exprs[0]) << ")";
        break;
    case K::BoundExtent:
        os << "bound_extent(" << vars[0].name << ", " << expr_str(exprs[0]) << ")";
        break;
    case K::BoundStorage:
        os << "bound_storage(" << vars[0].name << ", " << expr_str(exprs[0]) << ")";
        break;
    case K::SetEstimate:
        os << "set_estimate(" << vars[0].name << ", " << expr_str(exprs[0]) << ", "
           << expr_str(exprs[1]) << ")";
        break;
    case K::SetEstimates:
        os << "set_estimates({";
        for (size_t i = 0; i + 1 < exprs.size(); i += 2) {
            os << (i ? ", " : "") << "{" << expr_str(exprs[i]) << ", "
               << expr_str(exprs[i + 1]) << "}";
        }
        os << "})";
        break;
    case K::HoistStorage:
        os << "hoist_storage(" << at_func << ", " << at_var.name << ")";
        break;
    case K::HoistStorageRoot:
        os << "hoist_storage_root()";
        break;
    case K::TraceLoads:
        os << "trace_loads()";
        break;
    case K::TraceStores:
        os << "trace_stores()";
        break;
    case K::TraceRealizations:
        os << "trace_realizations()";
        break;
    case K::AddTraceTag:
        os << "add_trace_tag(\"" << message << "\")";
        break;
    case K::NoProfiling:
        os << "no_profiling()";
        break;
    case K::CopyToHost:
        os << "copy_to_host()";
        break;
    case K::CopyToDevice:
        os << "copy_to_device()";
        break;
    case K::ComputeWith:
        os << "compute_with(" << at_func << ", " << at_var.name << ")";
        break;
    case K::Prefetch:
        os << "prefetch(" << (ref_funcs.empty() ? "" : ref_funcs[0]) << ", " << vars[0].name
           << ", " << vars[1].name << ", " << expr_str(exprs[0]) << ")";
        break;
    case K::Specialize:
        os << "specialize(" << expr_str(exprs[0]) << ")";
        break;
    case K::SpecializeFail:
        os << "specialize_fail(\"" << message << "\")";
        break;
    case K::Rfactor:
        os << "rfactor({";
        for (size_t i = 0; i + 1 < vars.size(); i += 2) {
            os << (i ? ", " : "") << "{" << vars[i].name << ", " << vars[i + 1].name << "}";
        }
        os << "}) /* -> " << produces << " */";
        break;
    case K::In:
        os << "in(";
        func_list(os);
        os << ") /* -> " << produces << " */";
        break;
    case K::CloneIn:
        os << "clone_in(";
        func_list(os);
        os << ") /* -> " << produces << " */";
        break;
    }
    os << ";";
    return os.str();
}

}  // namespace Halide
