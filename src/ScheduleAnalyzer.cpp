#include "ScheduleAnalyzer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

#include "Definition.h"
#include "Error.h"
#include "FindCalls.h"
#include "Function.h"
#include "IR.h"
#include "Schedule.h"

namespace Halide {

namespace {

// The reachable Functions of `funcs` (outputs plus everything transitively
// called), de-duplicated by base name and returned in a stable order.
std::vector<Internal::Function> reachable_functions(const std::vector<Func> &funcs) {
    std::vector<Internal::Function> result;
    std::set<std::string> seen;
    auto add = [&](const Internal::Function &fn) {
        if (seen.insert(ScheduleAnalyzer::base_name(fn.name())).second) {
            result.push_back(fn);
        }
    };
    for (const Func &f : funcs) {
        Internal::Function fn = f.function();
        add(fn);
        for (const auto &kv : Internal::find_transitive_calls(fn)) {
            add(kv.second);
        }
    }
    return result;
}

// Split dims are stored qualified as "old.new" (e.g. "y.yo"), but Halide matches
// user Var names by their last dotted component and LoopLevels/directives use
// that plain name. Reduce a stored dim name to the name a directive would use.
std::string dequalify(const std::string &name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

// The loop-var names of one definition's schedule (skipping the implicit
// outermost loop).
std::set<std::string> dim_vars(const Internal::StageSchedule &schedule) {
    std::set<std::string> vars;
    for (const Internal::Dim &d : schedule.dims()) {
        if (d.var != "__outermost") {
            vars.insert(dequalify(d.var));
        }
    }
    return vars;
}

int num_stages(const Internal::Function &fn) {
    return 1 + (int)fn.updates().size();
}

const Internal::Definition &stage_def(const Internal::Function &fn, int stage) {
    return stage == 0 ? fn.definition() : fn.updates()[stage - 1];
}

const Internal::StageSchedule &stage_schedule(const Internal::Function &fn, int stage) {
    return stage_def(fn, stage).schedule();
}

VarSpec dim_spec(const Internal::Dim &d) {
    return VarSpec(dequalify(d.var), d.is_rvar());
}

VarSpec level_var(const LoopLevel &l) {
    return VarSpec(dequalify(l.var_name()), l.is_rvar());
}

// Emit the loop-transform directives for one StageSchedule, stamping each with
// the enclosing specialize() condition path.
void emit_stage_schedule(const std::string &name, const Internal::StageSchedule &ss, int s,
                         const std::vector<Expr> &path, ScheduleDirectives &out) {
    const size_t start = out.size();
    {
        // Splits, renames, and fuses are kept in one list to preserve ordering.
        auto qv = [](const std::string &n, bool rv) { return VarSpec(dequalify(n), rv); };
        for (const Internal::Split &sp : ss.splits()) {
            const bool rv = sp.exact;  // exact splits come from RVars
            switch (sp.split_type) {
            case Internal::Split::SplitVar:
                out.push_back(ScheduleDirective::split(name, qv(sp.old_var, rv), qv(sp.outer, rv),
                                                       qv(sp.inner, rv), sp.factor, sp.tail, s));
                break;
            case Internal::Split::RenameVar:
                out.push_back(ScheduleDirective::rename(name, qv(sp.old_var, rv), qv(sp.outer, rv), s));
                break;
            case Internal::Split::FuseVars:
                out.push_back(ScheduleDirective::fuse(name, qv(sp.inner, rv), qv(sp.outer, rv),
                                                      qv(sp.old_var, rv), s));
                break;
            }
        }

        // The dims list (innermost first, minus __outermost) is the loop order.
        std::vector<VarSpec> order;
        for (const Internal::Dim &d : ss.dims()) {
            if (d.var != "__outermost") {
                order.push_back(dim_spec(d));
            }
        }
        if (order.size() >= 2) {
            out.push_back(ScheduleDirective::reorder(name, order, s));
        }

        // Loop type and partition policy per dim.
        for (const Internal::Dim &d : ss.dims()) {
            if (d.var == "__outermost") {
                continue;
            }
            VarSpec v = dim_spec(d);
            switch (d.for_type) {
            case Internal::ForType::Parallel:
                out.push_back(ScheduleDirective::parallel(name, v, s));
                break;
            case Internal::ForType::Vectorized:
                out.push_back(ScheduleDirective::vectorize(name, v, s));
                break;
            case Internal::ForType::Unrolled:
                out.push_back(ScheduleDirective::unroll(name, v, s));
                break;
            case Internal::ForType::GPUBlock:
                out.push_back(ScheduleDirective::gpu_blocks(name, {v}, d.device_api, s));
                break;
            case Internal::ForType::GPUThread:
                out.push_back(ScheduleDirective::gpu_threads(name, {v}, d.device_api, s));
                break;
            case Internal::ForType::GPULane:
                out.push_back(ScheduleDirective::gpu_lanes(name, v, d.device_api, s));
                break;
            default:
                break;  // Serial (the default) and Extern (internal)
            }
            if (d.partition_policy == Partition::Never) {
                out.push_back(ScheduleDirective::never_partition(name, {v}, s));
            } else if (d.partition_policy == Partition::Always) {
                out.push_back(ScheduleDirective::always_partition(name, {v}, s));
            }
        }

        if (ss.atomic()) {
            out.push_back(ScheduleDirective::atomic(name, ss.override_atomic_associativity_test(), s));
        }
        if (ss.allow_race_conditions()) {
            out.push_back(ScheduleDirective::allow_race_conditions(name, s));
        }

        // Prefetches attached to this stage. `at`/`from` name loop vars of this
        // stage; recover their rvar-ness from the dims list.
        auto stage_var = [&](const std::string &vn) {
            for (const Internal::Dim &d : ss.dims()) {
                if (dequalify(d.var) == dequalify(vn)) {
                    return VarSpec(dequalify(vn), d.is_rvar());
                }
            }
            return VarSpec(dequalify(vn));
        };
        for (const Internal::PrefetchDirective &pd : ss.prefetches()) {
            out.push_back(ScheduleDirective::prefetch(name, ScheduleAnalyzer::base_name(pd.name),
                                                      stage_var(pd.at), stage_var(pd.from), pd.offset,
                                                      pd.strategy, s));
        }
    }
    for (size_t i = start; i < out.size(); i++) {
        out[i].specialize_conditions = path;
    }
}

// Emit one Definition -- its base schedule, plus any specialize() branches (each
// recorded with its condition path so it re-applies inside that scope).
void emit_definition(const std::string &name, const Internal::Definition &def, int s,
                     const std::vector<Expr> &path, ScheduleDirectives &out) {
    emit_stage_schedule(name, def.schedule(), s, path, out);
    for (const Internal::Specialization &spec : def.specializations()) {
        if (!spec.failure_message.empty()) {
            ScheduleDirective d = ScheduleDirective::specialize_fail(name, spec.failure_message, s);
            d.specialize_conditions = path;
            out.push_back(d);
            continue;
        }
        ScheduleDirective d = ScheduleDirective::specialize(name, spec.condition, s);
        d.specialize_conditions = path;
        out.push_back(d);
        std::vector<Expr> child = path;
        child.push_back(spec.condition);
        emit_definition(name, spec.definition, s, child, out);
    }
}

// Pass 1: loop transforms for every stage, including specialize() branches.
void emit_loops(const Internal::Function &fn, ScheduleDirectives &out) {
    const std::string name = ScheduleAnalyzer::base_name(fn.name());
    for (int s = 0; s < num_stages(fn); s++) {
        emit_definition(name, stage_def(fn, s), s, {}, out);
    }
}

// Pass 2: compute/store placement and compute_with (these reference loop vars
// created in pass 1, possibly on other Funcs).
void emit_placement(const Internal::Function &fn, ScheduleDirectives &out) {
    const std::string name = ScheduleAnalyzer::base_name(fn.name());
    const Internal::FuncSchedule &fs = fn.schedule();

    // Inspect LoopLevels through their raw name accessors, which -- unlike
    // is_root()/is_inlined()/defined() -- don't require the level to be locked
    // (loop levels are only locked during lowering). A non-empty func_name is a
    // real compute-at site; var_name "__root" is root; empty is inline/default.
    const LoopLevel &compute = fs.compute_level();
    const std::string cf = compute.func_name(), cv = compute.var_name();
    bool placed = false;
    if (!cf.empty()) {
        out.push_back(ScheduleDirective::compute_at(name, ScheduleAnalyzer::base_name(cf),
                                                    level_var(compute), compute.get_stage_index()));
        placed = true;
    } else if (cv == "__root") {
        out.push_back(ScheduleDirective::compute_root(name));
        placed = true;
    }
    if (placed) {
        const LoopLevel &store = fs.store_level();
        const std::string sf = store.func_name(), sv = store.var_name();
        const bool same = sf == cf && sv == cv && store.get_stage_index() == compute.get_stage_index();
        if (!same) {
            if (!sf.empty()) {
                out.push_back(ScheduleDirective::store_at(name, ScheduleAnalyzer::base_name(sf),
                                                          level_var(store), store.get_stage_index()));
            } else if (sv == "__root") {
                out.push_back(ScheduleDirective::store_root(name));
            }
        }
    }

    // hoist_storage, if set away from its inlined default.
    const LoopLevel &hoist = fs.hoist_storage_level();
    const std::string hf = hoist.func_name(), hv = hoist.var_name();
    if (!hf.empty()) {
        out.push_back(ScheduleDirective::hoist_storage(name, ScheduleAnalyzer::base_name(hf),
                                                       level_var(hoist), hoist.get_stage_index()));
    } else if (hv == "__root") {
        out.push_back(ScheduleDirective::hoist_storage_root(name));
    }

    for (int s = 0; s < num_stages(fn); s++) {
        const LoopLevel &fl = stage_schedule(fn, s).fuse_level().level;
        if (!fl.func_name().empty()) {
            out.push_back(ScheduleDirective::compute_with(name, ScheduleAnalyzer::base_name(fl.func_name()),
                                                          level_var(fl), LoopAlignStrategy::Auto,
                                                          fl.get_stage_index(), s));
        }
    }
}

// Pass 3: storage layout, bounds/estimates, and whole-Func properties.
void emit_storage(const Internal::Function &fn, ScheduleDirectives &out) {
    const std::string name = ScheduleAnalyzer::base_name(fn.name());
    const Internal::FuncSchedule &fs = fn.schedule();

    // A non-default storage order shows up as a reorder_storage.
    std::vector<std::string> storage_order, arg_order = fn.args();
    std::vector<VarSpec> storage_vars;
    for (const Internal::StorageDim &sd : fs.storage_dims()) {
        storage_order.push_back(dequalify(sd.var));
        storage_vars.push_back(VarSpec(dequalify(sd.var)));
    }
    if (storage_order != arg_order && storage_vars.size() >= 2) {
        out.push_back(ScheduleDirective::reorder_storage(name, storage_vars));
    }
    for (const Internal::StorageDim &sd : fs.storage_dims()) {
        if (sd.alignment.defined()) {
            out.push_back(ScheduleDirective::align_storage(name, VarSpec(dequalify(sd.var)), sd.alignment));
        }
        if (sd.bound.defined()) {
            out.push_back(ScheduleDirective::bound_storage(name, VarSpec(dequalify(sd.var)), sd.bound));
        }
        if (sd.fold_factor.defined()) {
            out.push_back(ScheduleDirective::fold_storage(name, VarSpec(dequalify(sd.var)), sd.fold_factor,
                                                          sd.fold_forward));
        }
    }

    for (const Internal::Bound &b : fs.bounds()) {
        if (b.min.defined() && b.extent.defined()) {
            out.push_back(ScheduleDirective::bound(name, VarSpec(dequalify(b.var)), b.min, b.extent));
        } else if (b.extent.defined()) {
            out.push_back(ScheduleDirective::bound_extent(name, VarSpec(dequalify(b.var)), b.extent));
        } else if (b.modulus.defined() && b.remainder.defined()) {
            out.push_back(ScheduleDirective::align_bounds(name, VarSpec(dequalify(b.var)), b.modulus, b.remainder));
        } else if (b.modulus.defined()) {
            out.push_back(ScheduleDirective::align_extent(name, VarSpec(dequalify(b.var)), b.modulus));
        }
    }
    for (const Internal::Bound &b : fs.estimates()) {
        if (b.min.defined() && b.extent.defined()) {
            out.push_back(ScheduleDirective::set_estimate(name, VarSpec(dequalify(b.var)), b.min, b.extent));
        }
    }

    if (fs.memory_type() != MemoryType::Auto) {
        out.push_back(ScheduleDirective::store_in(name, fs.memory_type()));
    }
    if (fs.memoized()) {
        out.push_back(ScheduleDirective::memoize(name));
    }
    if (fs.async()) {
        out.push_back(ScheduleDirective::async(name));
    }
    if (fs.ring_buffer().defined()) {
        out.push_back(ScheduleDirective::ring_buffer(name, fs.ring_buffer()));
    }
}

}  // namespace

std::string ScheduleAnalyzer::base_name(const std::string &name) {
    size_t dollar = name.find('$');
    return dollar == std::string::npos ? name : name.substr(0, dollar);
}

ScheduleAnalyzer::ScheduleAnalyzer(const std::vector<Func> &funcs) {
    const std::vector<Internal::Function> all = reachable_functions(funcs);
    for (const Internal::Function &fn : all) {
        std::vector<std::set<std::string>> stages;
        stages.push_back(dim_vars(fn.definition().schedule()));
        for (const Internal::Definition &u : fn.updates()) {
            stages.push_back(dim_vars(u.schedule()));
        }
        vars_[base_name(fn.name())] = std::move(stages);
    }
    // Emit in dependency order across all Funcs: loop transforms first (they
    // create the vars), then compute/store placement (which references those
    // vars, possibly on other Funcs), then storage and whole-Func properties.
    for (const Internal::Function &fn : all) {
        emit_loops(fn, directives_);
    }
    for (const Internal::Function &fn : all) {
        emit_placement(fn, directives_);
    }
    for (const Internal::Function &fn : all) {
        emit_storage(fn, directives_);
    }
}

ScheduleAnalyzer::ScheduleAnalyzer(const Func &func)
    : ScheduleAnalyzer(std::vector<Func>{func}) {
}

ScheduleAnalyzer::ScheduleAnalyzer(const Pipeline &pipeline)
    : ScheduleAnalyzer(pipeline.outputs()) {
}

ScheduleAnalyzer::ScheduleAnalyzer(const ScheduleDirectives &directives)
    : directives_(directives) {
    // Derive the Func/var model from what the directives reference and
    // introduce (there are no Func schedules to read here).
    auto touch = [&](const std::string &func, int stage) -> std::set<std::string> & {
        std::vector<std::set<std::string>> &stages = vars_[base_name(func)];
        int s = stage < 0 ? 0 : stage;
        while ((int)stages.size() <= s) {
            stages.emplace_back();
        }
        return stages[s];
    };
    for (const ScheduleDirective &d : directives_) {
        std::set<std::string> &vs = touch(d.func, d.stage);
        for (const VarSpec &v : d.vars) {
            vs.insert(v.name);
        }
        if (!d.at_func.empty()) {
            std::set<std::string> &at = touch(d.at_func, d.at_stage);
            if (!d.at_var.name.empty()) {
                at.insert(d.at_var.name);
            }
        }
        for (const std::string &rf : d.ref_funcs) {
            touch(rf, 0);
        }
        if (!d.produces.empty()) {
            touch(d.produces, 0);
        }
    }
}

std::vector<std::string> ScheduleAnalyzer::func_names() const {
    std::vector<std::string> names;
    names.reserve(vars_.size());
    for (const auto &kv : vars_) {
        names.push_back(kv.first);
    }
    return names;  // std::map keeps keys sorted
}

bool ScheduleAnalyzer::has_func(const std::string &func) const {
    return vars_.count(base_name(func)) > 0;
}

int ScheduleAnalyzer::stage_count(const std::string &func) const {
    auto it = vars_.find(base_name(func));
    return it == vars_.end() ? 0 : (int)it->second.size();
}

std::vector<std::string> ScheduleAnalyzer::vars(const std::string &func, int stage) const {
    auto it = vars_.find(base_name(func));
    if (it == vars_.end() || stage < 0 || stage >= (int)it->second.size()) {
        return {};
    }
    const std::set<std::string> &s = it->second[stage];
    return {s.begin(), s.end()};
}

bool ScheduleAnalyzer::has_var(const std::string &func, const std::string &var) const {
    auto it = vars_.find(base_name(func));
    if (it == vars_.end()) {
        return false;
    }
    for (const std::set<std::string> &s : it->second) {
        if (s.count(var)) {
            return true;
        }
    }
    return false;
}

std::vector<size_t> ScheduleAnalyzer::directive_indices_for(const std::string &func) const {
    std::vector<size_t> result;
    const std::string base = base_name(func);
    for (size_t i = 0; i < directives_.size(); i++) {
        if (base_name(directives_[i].func) == base) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<size_t> ScheduleAnalyzer::directive_indices_for(const std::string &func, int stage) const {
    std::vector<size_t> result;
    for (size_t i : directive_indices_for(func)) {
        if (directives_[i].stage == stage) {
            result.push_back(i);
        }
    }
    return result;
}

std::string ScheduleAnalyzer::to_source(const ScheduleDirectives &directives) {
    std::ostringstream os;
    for (const ScheduleDirective &d : directives) {
        os << d.to_source() << "\n";
    }
    return os.str();
}

namespace {

// The one place directive kinds are paired with their JSON/string names; used
// in both directions.
const std::vector<std::pair<ScheduleDirective::Kind, const char *>> &kind_table() {
    using K = ScheduleDirective::Kind;
    static const std::vector<std::pair<K, const char *>> table = {
        {K::Split, "Split"},
        {K::Fuse, "Fuse"},
        {K::Rename, "Rename"},
        {K::Reorder, "Reorder"},
        {K::Tile, "Tile"},
        {K::Serial, "Serial"},
        {K::Parallel, "Parallel"},
        {K::Vectorize, "Vectorize"},
        {K::Unroll, "Unroll"},
        {K::Atomic, "Atomic"},
        {K::AllowRaceConditions, "AllowRaceConditions"},
        {K::GpuBlocks, "GpuBlocks"},
        {K::GpuThreads, "GpuThreads"},
        {K::GpuLanes, "GpuLanes"},
        {K::GpuTile, "GpuTile"},
        {K::Gpu, "Gpu"},
        {K::GpuSingleThread, "GpuSingleThread"},
        {K::Hexagon, "Hexagon"},
        {K::Partition, "Partition"},
        {K::NeverPartition, "NeverPartition"},
        {K::NeverPartitionAll, "NeverPartitionAll"},
        {K::AlwaysPartition, "AlwaysPartition"},
        {K::AlwaysPartitionAll, "AlwaysPartitionAll"},
        {K::Host, "Host"},
        {K::SmeStreaming, "SmeStreaming"},
        {K::StreamLoads, "StreamLoads"},
        {K::StreamStores, "StreamStores"},
        {K::EagerInline, "EagerInline"},
        {K::ComputeWith, "ComputeWith"},
        {K::Prefetch, "Prefetch"},
        {K::Specialize, "Specialize"},
        {K::SpecializeFail, "SpecializeFail"},
        {K::Rfactor, "Rfactor"},
        {K::In, "In"},
        {K::CloneIn, "CloneIn"},
        {K::ComputeAt, "ComputeAt"},
        {K::ComputeRoot, "ComputeRoot"},
        {K::ComputeInline, "ComputeInline"},
        {K::StoreAt, "StoreAt"},
        {K::StoreRoot, "StoreRoot"},
        {K::Bound, "Bound"},
        {K::AlignStorage, "AlignStorage"},
        {K::FoldStorage, "FoldStorage"},
        {K::ReorderStorage, "ReorderStorage"},
        {K::StoreIn, "StoreIn"},
        {K::Memoize, "Memoize"},
        {K::Async, "Async"},
        {K::RingBuffer, "RingBuffer"},
        {K::AlignBounds, "AlignBounds"},
        {K::AlignExtent, "AlignExtent"},
        {K::BoundExtent, "BoundExtent"},
        {K::BoundStorage, "BoundStorage"},
        {K::SetEstimate, "SetEstimate"},
        {K::SetEstimates, "SetEstimates"},
        {K::HoistStorage, "HoistStorage"},
        {K::HoistStorageRoot, "HoistStorageRoot"},
        {K::TraceLoads, "TraceLoads"},
        {K::TraceStores, "TraceStores"},
        {K::TraceRealizations, "TraceRealizations"},
        {K::AddTraceTag, "AddTraceTag"},
        {K::NoProfiling, "NoProfiling"},
        {K::CopyToHost, "CopyToHost"},
        {K::CopyToDevice, "CopyToDevice"},
    };
    return table;
}

std::string kind_to_string(ScheduleDirective::Kind k) {
    for (const auto &kv : kind_table()) {
        if (kv.first == k) {
            return kv.second;
        }
    }
    return "Unknown";
}

ScheduleDirective::Kind kind_from_string(const std::string &s) {
    for (const auto &kv : kind_table()) {
        if (s == kv.second) {
            return kv.first;
        }
    }
    user_error << "ScheduleAnalyzer::from_json: unknown directive kind '" << s << "'.";
    return ScheduleDirective::Kind::ComputeRoot;
}

void json_string(std::ostream &os, const std::string &s) {
    os << '"';
    for (char c : s) {
        switch (c) {
        case '"':
            os << "\\\"";
            break;
        case '\\':
            os << "\\\\";
            break;
        case '\n':
            os << "\\n";
            break;
        case '\t':
            os << "\\t";
            break;
        case '\r':
            os << "\\r";
            break;
        default:
            os << c;
        }
    }
    os << '"';
}

int64_t require_const_int(const Expr &e, const char *what) {
    const Internal::IntImm *i = e.as<Internal::IntImm>();
    user_assert(i) << "ScheduleAnalyzer::to_json: " << what
                   << " must be an integer constant to serialize to JSON.";
    return i->value;
}

void json_var(std::ostream &os, const VarSpec &v) {
    os << "{\"name\": ";
    json_string(os, v.name);
    os << ", \"rvar\": " << (v.is_rvar ? "true" : "false") << "}";
}

// A minimal recursive-descent JSON value + parser (no external dependency).
struct Json {
    enum Type { Null,
                Bool,
                Number,
                String,
                Array,
                Object } type = Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    const Json *get(const std::string &key) const {
        if (type != Object) {
            return nullptr;
        }
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }
    int64_t as_int() const {
        return (int64_t)number;
    }
};

struct JsonParser {
    const char *s;
    const char *e;

    [[noreturn]] void fail(const char *msg) {
        user_error << "ScheduleAnalyzer::from_json: " << msg << ".";
    }
    void ws() {
        while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) {
            s++;
        }
    }

    Json parse() {
        Json j = value();
        ws();
        return j;
    }

    Json value() {
        ws();
        if (s >= e) {
            fail("unexpected end of input");
        }
        switch (*s) {
        case '{':
            return object();
        case '[':
            return array();
        case '"': {
            Json j;
            j.type = Json::String;
            j.str = parse_string();
            return j;
        }
        case 't':
        case 'f': {
            Json j;
            j.type = Json::Bool;
            j.boolean = parse_bool();
            return j;
        }
        case 'n':
            expect("null");
            return Json{};
        default: {
            Json j;
            j.type = Json::Number;
            j.number = parse_number();
            return j;
        }
        }
    }

    Json object() {
        Json j;
        j.type = Json::Object;
        s++;  // '{'
        ws();
        if (s < e && *s == '}') {
            s++;
            return j;
        }
        while (true) {
            ws();
            std::string key = parse_string();
            ws();
            if (s >= e || *s != ':') {
                fail("expected ':'");
            }
            s++;
            j.obj[key] = value();
            ws();
            if (s < e && *s == ',') {
                s++;
                continue;
            }
            if (s < e && *s == '}') {
                s++;
                break;
            }
            fail("expected ',' or '}'");
        }
        return j;
    }

    Json array() {
        Json j;
        j.type = Json::Array;
        s++;  // '['
        ws();
        if (s < e && *s == ']') {
            s++;
            return j;
        }
        while (true) {
            j.arr.push_back(value());
            ws();
            if (s < e && *s == ',') {
                s++;
                continue;
            }
            if (s < e && *s == ']') {
                s++;
                break;
            }
            fail("expected ',' or ']'");
        }
        return j;
    }

    std::string parse_string() {
        if (s >= e || *s != '"') {
            fail("expected string");
        }
        s++;
        std::string out;
        while (s < e && *s != '"') {
            char c = *s++;
            if (c == '\\' && s < e) {
                char x = *s++;
                switch (x) {
                case 'n':
                    out += '\n';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'r':
                    out += '\r';
                    break;
                default:
                    out += x;  // covers " \ / and others
                }
            } else {
                out += c;
            }
        }
        if (s >= e) {
            fail("unterminated string");
        }
        s++;
        return out;
    }

    bool parse_bool() {
        if ((size_t)(e - s) >= 4 && strncmp(s, "true", 4) == 0) {
            s += 4;
            return true;
        }
        if ((size_t)(e - s) >= 5 && strncmp(s, "false", 5) == 0) {
            s += 5;
            return false;
        }
        fail("expected boolean");
    }

    double parse_number() {
        char *endp = nullptr;
        double d = std::strtod(s, &endp);
        if (endp == s) {
            fail("expected number");
        }
        s = endp;
        return d;
    }

    void expect(const char *lit) {
        size_t n = std::strlen(lit);
        if ((size_t)(e - s) < n || strncmp(s, lit, n) != 0) {
            fail("unexpected token");
        }
        s += n;
    }
};

VarSpec parse_var(const Json &j) {
    VarSpec v;
    if (const Json *n = j.get("name")) {
        v.name = n->str;
    }
    if (const Json *r = j.get("rvar")) {
        v.is_rvar = r->boolean;
    }
    return v;
}

}  // namespace

std::string ScheduleAnalyzer::to_json(const ScheduleDirectives &directives) {
    std::ostringstream os;
    os << "{\n  \"version\": 1,\n  \"directives\": [";
    const auto &ds = directives;
    for (size_t i = 0; i < ds.size(); i++) {
        const ScheduleDirective &d = ds[i];
        os << (i ? "," : "") << "\n    {\"kind\": ";
        json_string(os, kind_to_string(d.kind));
        os << ", \"func\": ";
        json_string(os, d.func);
        if (d.stage != 0) {
            os << ", \"stage\": " << d.stage;
        }
        if (!d.vars.empty()) {
            os << ", \"vars\": [";
            for (size_t j = 0; j < d.vars.size(); j++) {
                os << (j ? ", " : "");
                json_var(os, d.vars[j]);
            }
            os << "]";
        }
        if (!d.exprs.empty()) {
            os << ", \"exprs\": [";
            for (size_t j = 0; j < d.exprs.size(); j++) {
                os << (j ? ", " : "") << require_const_int(d.exprs[j], "expr");
            }
            os << "]";
        }
        if (d.tail != TailStrategy::Auto) {
            os << ", \"tail\": " << (int)d.tail;
        }
        if (d.device != DeviceAPI::Default_GPU) {
            os << ", \"device\": " << (int)d.device;
        }
        if (d.memory_type != MemoryType::Auto) {
            os << ", \"memory_type\": " << (int)d.memory_type;
        }
        if (d.flag) {
            os << ", \"flag\": true";
        }
        if (!d.at_func.empty()) {
            os << ", \"at_func\": ";
            json_string(os, d.at_func);
            os << ", \"at_var\": ";
            json_var(os, d.at_var);
            os << ", \"at_stage\": " << d.at_stage;
        }
        if (d.partition_policy != Partition::Auto) {
            os << ", \"partition\": " << (int)d.partition_policy;
        }
        if (!d.message.empty()) {
            os << ", \"message\": ";
            json_string(os, d.message);
        }
        if (!d.ref_funcs.empty()) {
            os << ", \"ref_funcs\": [";
            for (size_t j = 0; j < d.ref_funcs.size(); j++) {
                os << (j ? ", " : "");
                json_string(os, d.ref_funcs[j]);
            }
            os << "]";
        }
        if (d.align != LoopAlignStrategy::Auto) {
            os << ", \"align\": " << (int)d.align;
        }
        if (!d.var_aligns.empty()) {
            os << ", \"var_aligns\": [";
            for (size_t j = 0; j < d.var_aligns.size(); j++) {
                os << (j ? ", " : "") << (int)d.var_aligns[j];
            }
            os << "]";
        }
        if (d.prefetch_strategy != PrefetchBoundStrategy::GuardWithIf) {
            os << ", \"prefetch\": " << (int)d.prefetch_strategy;
        }
        if (!d.specialize_conditions.empty()) {
            os << ", \"specialize\": [";
            for (size_t j = 0; j < d.specialize_conditions.size(); j++) {
                os << (j ? ", " : "")
                   << require_const_int(d.specialize_conditions[j], "specialize condition");
            }
            os << "]";
        }
        if (!d.produces.empty()) {
            os << ", \"produces\": ";
            json_string(os, d.produces);
        }
        os << "}";
    }
    os << "\n  ]\n}\n";
    return os.str();
}

ScheduleDirectives ScheduleAnalyzer::from_json(const std::string &json) {
    JsonParser parser{json.data(), json.data() + json.size()};
    Json root = parser.parse();
    const Json *dirs = root.get("directives");
    user_assert(dirs && dirs->type == Json::Array)
        << "ScheduleAnalyzer::from_json: expected a top-level \"directives\" array.";

    ScheduleDirectives directives;
    for (const Json &jd : dirs->arr) {
        ScheduleDirective d;
        const Json *kind = jd.get("kind");
        user_assert(kind && kind->type == Json::String)
            << "ScheduleAnalyzer::from_json: each directive needs a \"kind\".";
        d.kind = kind_from_string(kind->str);
        if (const Json *j = jd.get("func")) {
            d.func = j->str;
        }
        if (const Json *j = jd.get("stage")) {
            d.stage = (int)j->as_int();
        }
        if (const Json *j = jd.get("vars")) {
            for (const Json &v : j->arr) {
                d.vars.push_back(parse_var(v));
            }
        }
        if (const Json *j = jd.get("exprs")) {
            for (const Json &v : j->arr) {
                d.exprs.push_back(Expr((int)v.as_int()));
            }
        }
        if (const Json *j = jd.get("tail")) {
            d.tail = (TailStrategy)j->as_int();
        }
        if (const Json *j = jd.get("device")) {
            d.device = (DeviceAPI)j->as_int();
        }
        if (const Json *j = jd.get("memory_type")) {
            d.memory_type = (MemoryType)j->as_int();
        }
        if (const Json *j = jd.get("flag")) {
            d.flag = j->boolean;
        }
        if (const Json *j = jd.get("at_func")) {
            d.at_func = j->str;
        }
        if (const Json *j = jd.get("at_var")) {
            d.at_var = parse_var(*j);
        }
        if (const Json *j = jd.get("at_stage")) {
            d.at_stage = (int)j->as_int();
        }
        if (const Json *j = jd.get("partition")) {
            d.partition_policy = (Partition)j->as_int();
        }
        if (const Json *j = jd.get("message")) {
            d.message = j->str;
        }
        if (const Json *j = jd.get("ref_funcs")) {
            for (const Json &v : j->arr) {
                d.ref_funcs.push_back(v.str);
            }
        }
        if (const Json *j = jd.get("align")) {
            d.align = (LoopAlignStrategy)j->as_int();
        }
        if (const Json *j = jd.get("var_aligns")) {
            for (const Json &v : j->arr) {
                d.var_aligns.push_back((LoopAlignStrategy)v.as_int());
            }
        }
        if (const Json *j = jd.get("prefetch")) {
            d.prefetch_strategy = (PrefetchBoundStrategy)j->as_int();
        }
        if (const Json *j = jd.get("specialize")) {
            for (const Json &v : j->arr) {
                d.specialize_conditions.push_back(Expr((int)v.as_int()));
            }
        }
        if (const Json *j = jd.get("produces")) {
            d.produces = j->str;
        }
        directives.push_back(std::move(d));
    }
    return directives;
}

}  // namespace Halide
