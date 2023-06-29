#include "IRComparator.h"
#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IREquality.h"
#include "IRVisitor.h"
#include <map>
#include <string>
#include <unordered_set>
#include <utility>

namespace Halide {
namespace Internal {
class IRComparator {
public:
    IRComparator() {
    }

    bool compare_pipeline(const Pipeline &p1, const Pipeline &p2);

private:
    struct FunctionContentsEq {
        bool operator()(FunctionContents const *lhs, FunctionContents const *rhs) const {
            return lhs == rhs;
        }
    };

    // std::unordered_set<std::pair<FunctionContents *, FunctionContents *>, FunctionContentsEq> func_visited;

    bool compare_function(const Function &f1, const Function &f2);

    bool compare_type(const Type &t1, const Type &t2);

    bool compare_func_schedule(const FuncSchedule &fs1, const FuncSchedule &fs2);

    bool compare_loop_level(const LoopLevel &l1, const LoopLevel &l2);

    bool compare_storage_dim(const StorageDim &sd1, const StorageDim &sd2);

    bool compare_bound(const Bound &b1, const Bound &b2);

    bool compare_definition(const Definition &d1, const Definition &d2);

    bool compare_stage_schedule(const StageSchedule &ss1, const StageSchedule &ss2);

    bool compare_specialization(const Specialization &s1, const Specialization &s2);

    bool compare_reduction_variable(const ReductionVariable &rv1, const ReductionVariable &rv2);

    bool compare_split(const Split &s1, const Split &s2);

    bool compare_dim(const Dim &d1, const Dim &d2);

    bool compare_prefetch_directive(const PrefetchDirective &pd1, const PrefetchDirective &pd2);

    bool compare_fuse_loop_level(const FuseLoopLevel &fl1, const FuseLoopLevel &fl2);

    bool compare_fused_pair(const FusedPair &fp1, const FusedPair &fp2);

    bool compare_parameter(const Parameter &p1, const Parameter &p2);
};

bool IRComparator::compare_pipeline(const Pipeline &p1, const Pipeline &p2) {
    std::map<std::string, Function> p1_env, p2_env;
    if (p1.outputs().size() != p2.outputs().size()) {
        debug(0) << "Outputs size not equal, p1 size: " << p1.outputs().size() << ", p2 size: " << p2.outputs().size() << "\n";
        return false;
    }
    for (const Func &func : p1.outputs()) {
        const Halide::Internal::Function &f = func.function();
        std::map<std::string, Halide::Internal::Function> more_funcs = find_transitive_calls(f);
        p1_env.insert(more_funcs.begin(), more_funcs.end());
    }
    for (const Func &func : p2.outputs()) {
        const Halide::Internal::Function &f = func.function();
        std::map<std::string, Halide::Internal::Function> more_funcs = find_transitive_calls(f);
        p2_env.insert(more_funcs.begin(), more_funcs.end());
    }
    if (p1_env.size() != p2_env.size()) {
        debug(0) << "DAG size not equal, p1 size: " << p1_env.size() << ", p2 size: " << p2_env.size() << "\n";
        return false;
    }
    for (auto it = p1_env.begin(); it != p1_env.end(); it++) {
        if (p2_env.find(it->first) == p2_env.end()) {
            debug(0) << "Function " << it->first << " in p1 not found in p2\n";
            return false;
        }
        if (!compare_function(it->second, p2_env[it->first])) {
            debug(0) << "Function " << it->first << " not equal\n";
            return false;
        }
    }
    for (size_t i = 0; i < p1.requirements().size() && i < p2.requirements().size(); i++) {
        if (!equal(p1.requirements()[i], p2.requirements()[i])) {
            debug(0) << "Requirement (Stmt) " << i << " not equal\n";
            return false;
        }
    }
    return true;
}

bool IRComparator::compare_function(const Function &f1, const Function &f2) {
    // if (this->func_visited.find(std::make_pair(f1.get_contents().get(), f2.get_contents().get())) != this->func_visited.end()) {
    //     return true;
    // }
    if (f1.name() != f2.name()) {
        return false;
    }
    if (f1.origin_name() != f2.origin_name()) {
        return false;
    }
    if (f1.output_types().size() != f2.output_types().size()) {
        return false;
    }
    for (size_t i = 0; i < f1.output_types().size(); i++) {
        if (!compare_type(f1.output_types()[i], f2.output_types()[i])) {
            return false;
        }
    }
    if (f1.required_types().size() != f2.required_types().size()) {
        return false;
    }
    for (size_t i = 0; i < f1.required_types().size(); i++) {
        if (!compare_type(f1.required_types()[i], f2.required_types()[i])) {
            return false;
        }
    }
    if (f1.required_dimensions() != f2.required_dimensions()) {
        return false;
    }
    if (f1.args().size() != f2.args().size()) {
        return false;
    }
    for (size_t i = 0; i < f1.args().size(); i++) {
        if (!equal(f1.args()[i], f2.args()[i])) {
            return false;
        }
    }
    if (!compare_func_schedule(f1.schedule(), f2.schedule())) {
        return false;
    }
    if (!compare_definition(f1.definition(), f2.definition())) {
        return false;
    }
    if (f1.updates().size() != f2.updates().size()) {
        return false;
    }
    if (f1.debug_file() != f2.debug_file()) {
        return false;
    }
    if (f1.output_buffers().size() != f2.output_buffers().size()) {
        return false;
    }
    if (f1.extern_arguments().size() != f2.extern_arguments().size()) {
        return false;
    }
    if (f1.extern_function_name() != f2.extern_function_name()) {
        return false;
    }
    if (f1.extern_definition_name_mangling() != f2.extern_definition_name_mangling()) {
        return false;
    }
    if (f1.extern_function_device_api() != f2.extern_function_device_api()) {
        return false;
    }
    if (!equal(f1.extern_definition_proxy_expr(), f2.extern_definition_proxy_expr())) {
        return false;
    }
    if (f1.is_tracing_loads() != f2.is_tracing_loads()) {
        return false;
    }
    if (f1.is_tracing_stores() != f2.is_tracing_stores()) {
        return false;
    }
    if (f1.is_tracing_realizations() != f2.is_tracing_realizations()) {
        return false;
    }
    if (f1.get_trace_tags().size() != f2.get_trace_tags().size()) {
        return false;
    }
    for (size_t i = 0; i < f1.get_trace_tags().size(); i++) {
        if (f1.get_trace_tags()[i] != f2.get_trace_tags()[i]) {
            return false;
        }
    }
    if (f1.frozen() != f2.frozen()) {
        return false;
    }
    return true;
}

bool IRComparator::compare_type(const Type &t1, const Type &t2) {
    // TODO: what about handle_type?
    if (t1.code() != t2.code()) {
        return false;
    }
    if (t1.bits() != t2.bits()) {
        return false;
    }
    if (t1.lanes() != t2.lanes()) {
        return false;
    }
    return true;
}

bool IRComparator::compare_func_schedule(const FuncSchedule &fs1, const FuncSchedule &fs2) {
    if (!compare_loop_level(fs1.store_level(), fs2.store_level())) {
        return false;
    }
    if (!compare_loop_level(fs1.compute_level(), fs2.compute_level())) {
        return false;
    }
    if (fs1.storage_dims().size() != fs2.storage_dims().size()) {
        debug(0) << "storage_dims size not equal, fs1 size: " << fs1.storage_dims().size() << ", fs2 size: " << fs2.storage_dims().size() << "\n";
        return false;
    }
    for (size_t i = 0; i < fs1.storage_dims().size(); i++) {
        if (!compare_storage_dim(fs1.storage_dims()[i], fs2.storage_dims()[i])) {
            return false;
        }
    }
    if (fs1.bounds().size() != fs2.bounds().size()) {
        debug(0) << "bounds size not equal, fs1 size: " << fs1.bounds().size() << ", fs2 size: " << fs2.bounds().size() << "\n";
        return false;
    }
    for (size_t i = 0; i < fs1.bounds().size(); i++) {
        if (!compare_bound(fs1.bounds()[i], fs2.bounds()[i])) {
            return false;
        }
    }
    if (fs1.estimates().size() != fs2.estimates().size()) {
        debug(0) << "estimates size not equal, fs1 size: " << fs1.estimates().size() << ", fs2 size: " << fs2.estimates().size() << "\n";
        return false;
    }
    for (size_t i = 0; i < fs1.estimates().size(); i++) {
        if (!compare_bound(fs1.estimates()[i], fs2.estimates()[i])) {
            return false;
        }
    }
    if (fs1.wrappers().size() != fs2.wrappers().size()) {
        debug(0) << "wrappers size not equal, fs1 size: " << fs1.wrappers().size() << ", fs2 size: " << fs2.wrappers().size() << "\n";
        return false;
    }
    // TODO: implement function ptr comparison
    if (fs1.memory_type() != fs2.memory_type()) {
        return false;
    }
    if (fs1.memoized() != fs2.memoized()) {
        return false;
    }
    if (fs1.async() != fs2.async()) {
        return false;
    }
    if (!equal(fs1.memoize_eviction_key(), fs2.memoize_eviction_key())) {
        return false;
    }
    return true;
}

bool IRComparator::compare_loop_level(const LoopLevel &l1, const LoopLevel &l2) {
    if (l1.func_name() != l2.func_name()) {
        return false;
    }
    if (l1.get_stage_index() != l2.get_stage_index()) {
        return false;
    }
    if (l1.var_name() != l2.var_name()) {
        return false;
    }
    if (l1.is_rvar() != l2.is_rvar()) {
        return false;
    }
    if (l1.locked() != l2.locked()) {
        return false;
    }
    return true;
}

bool IRComparator::compare_storage_dim(const StorageDim &sd1, const StorageDim &sd2) {
    if (sd1.var != sd2.var) {
        return false;
    }
    if (!equal(sd1.alignment, sd2.alignment)) {
        return false;
    }
    if (!equal(sd1.bound, sd2.bound)) {
        return false;
    }
    if (!equal(sd1.fold_factor, sd2.fold_factor)) {
        return false;
    }
    if (sd1.fold_forward != sd2.fold_forward) {
        return false;
    }
    return true;
}

bool IRComparator::compare_bound(const Bound &b1, const Bound &b2) {
    if (b1.var != b2.var) {
        return false;
    }
    if (!equal(b1.min, b2.min)) {
        return false;
    }
    if (!equal(b1.extent, b2.extent)) {
        return false;
    }
    if (!equal(b1.modulus, b2.modulus)) {
        return false;
    }
    if (!equal(b1.remainder, b2.remainder)) {
        return false;
    }
    return true;
}

bool IRComparator::compare_definition(const Definition &d1, const Definition &d2) {
    if (d1.is_init() != d2.is_init()) {
        debug(0) << "is_init not equal\n";
        return false;
    }
    if (!equal(d1.predicate(), d2.predicate())) {
        debug(0) << "predicate not equal, d1 predicate: " << d1.predicate() << ", d2 predicate: " << d2.predicate() << "\n";
        return false;
    }
    if (d1.values().size() != d2.values().size()) {
        debug(0) << "values size not equal, d1 size: " << d1.values().size() << ", d2 size: " << d2.values().size() << "\n";
        return false;
    }
    for (size_t i = 0; i < d1.values().size(); i++) {
        if (!equal(d1.values()[i], d2.values()[i])) {
            debug(0) << "value " << i << " not equal\n";
            return false;
        }
    }
    if (d1.args().size() != d2.args().size()) {
        debug(0) << "args size not equal, d1 size: " << d1.args().size() << ", d2 size: " << d2.args().size() << "\n";
        return false;
    }
    for (size_t i = 0; i < d1.args().size(); i++) {
        if (!equal(d1.args()[i], d2.args()[i])) {
            debug(0) << "arg " << i << " not equal\n";
            return false;
        }
    }
    if (!compare_stage_schedule(d1.schedule(), d2.schedule())) {
        debug(0) << "schedule not equal\n";
        return false;
    }
    if (d1.specializations().size() != d2.specializations().size()) {
        debug(0) << "specializations size not equal, d1 size: " << d1.specializations().size() << ", d2 size: " << d2.specializations().size() << "\n";
        return false;
    }
    for (size_t i = 0; i < d1.specializations().size(); i++) {
        if (!compare_specialization(d1.specializations()[i], d2.specializations()[i])) {
            debug(0) << "specialization " << i << " not equal\n";
            return false;
        }
    }
    if (d1.source_location() != d2.source_location()) {
        return false;
    }
    return true;
}

bool IRComparator::compare_stage_schedule(const StageSchedule &ss1, const StageSchedule &ss2) {
    if (ss1.rvars().size() != ss2.rvars().size()) {
        return false;
    }
    for (size_t i = 0; i < ss1.rvars().size(); i++) {
        if (!compare_reduction_variable(ss1.rvars()[i], ss2.rvars()[i])) {
            return false;
        }
    }
    if (ss1.splits().size() != ss2.splits().size()) {
        return false;
    }
    for (size_t i = 0; i < ss1.splits().size(); i++) {
        if (!compare_split(ss1.splits()[i], ss2.splits()[i])) {
            return false;
        }
    }
    if (ss1.dims().size() != ss2.dims().size()) {
        return false;
    }
    for (size_t i = 0; i < ss1.dims().size(); i++) {
        if (!compare_dim(ss1.dims()[i], ss2.dims()[i])) {
            return false;
        }
    }
    if (ss1.prefetches().size() != ss2.prefetches().size()) {
        return false;
    }
    for (size_t i = 0; i < ss1.prefetches().size(); i++) {
        if (!compare_prefetch_directive(ss1.prefetches()[i], ss2.prefetches()[i])) {
            return false;
        }
    }
    if (!compare_fuse_loop_level(ss1.fuse_level(), ss2.fuse_level())) {
        return false;
    }
    if (ss1.fused_pairs().size() != ss2.fused_pairs().size()) {
        return false;
    }
    for (size_t i = 0; i < ss1.fused_pairs().size(); i++) {
        if (!compare_fused_pair(ss1.fused_pairs()[i], ss2.fused_pairs()[i])) {
            return false;
        }
    }
    if (ss1.touched() != ss2.touched()) {
        return false;
    }
    if (ss1.allow_race_conditions() != ss2.allow_race_conditions()) {
        return false;
    }
    if (ss1.atomic() != ss2.atomic()) {
        return false;
    }
    if (ss1.override_atomic_associativity_test() != ss2.override_atomic_associativity_test()) {
        return false;
    }
    return true;
}

bool IRComparator::compare_specialization(const Specialization &s1, const Specialization &s2) {
    if (!equal(s1.condition, s2.condition)) {
        return false;
    }
    if (!compare_definition(s1.definition, s2.definition)) {
        return false;
    }
    if (s1.failure_message != s2.failure_message) {
        return false;
    }
    return true;
}

bool IRComparator::compare_reduction_variable(const ReductionVariable &rv1, const ReductionVariable &rv2) {
    if (rv1.var != rv2.var) {
        return false;
    }
    if (!equal(rv1.min, rv2.min)) {
        return false;
    }
    if (!equal(rv1.extent, rv2.extent)) {
        return false;
    }
    return true;
}

bool IRComparator::compare_split(const Split &s1, const Split &s2) {
    if (s1.old_var != s2.old_var) {
        return false;
    }
    if (s1.outer != s2.outer) {
        return false;
    }
    if (s1.inner != s2.inner) {
        return false;
    }
    if (!equal(s1.factor, s2.factor)) {
        return false;
    }
    if (s1.tail != s2.tail) {
        return false;
    }
    if (s1.split_type != s2.split_type) {
        return false;
    }
    return true;
}

bool IRComparator::compare_dim(const Dim &d1, const Dim &d2) {
    if (d1.var != d2.var) {
        return false;
    }
    if (d1.for_type != d2.for_type) {
        return false;
    }
    if (d1.device_api != d2.device_api) {
        return false;
    }
    if (d1.dim_type != d2.dim_type) {
        return false;
    }
    return true;
}

bool IRComparator::compare_prefetch_directive(const PrefetchDirective &pd1, const PrefetchDirective &pd2) {
    if (pd1.name != pd2.name) {
        return false;
    }
    if (pd1.at != pd2.at) {
        return false;
    }
    if (pd1.from != pd2.from) {
        return false;
    }
    if (!equal(pd1.offset, pd2.offset)) {
        return false;
    }
    if (pd1.strategy != pd2.strategy) {
        return false;
    }
    if (!compare_parameter(pd1.param, pd2.param)) {
        return false;
    }
    return true;
}

bool IRComparator::compare_fuse_loop_level(const FuseLoopLevel &fl1, const FuseLoopLevel &fl2) {
    if (!compare_loop_level(fl1.level, fl2.level)) {
        return false;
    }
    if (fl1.align.size() != fl2.align.size()) {
        return false;
    }
    for (auto it = fl1.align.begin(); it != fl1.align.end(); it++) {
        if (fl2.align.find(it->first) == fl2.align.end()) {
            return false;
        }
        if (it->second != fl2.align.at(it->first)) {
            return false;
        }
    }
    return true;
}

bool IRComparator::compare_fused_pair(const FusedPair &fp1, const FusedPair &fp2) {
    if (fp1.func_1 != fp2.func_1) {
        return false;
    }
    if (fp1.func_2 != fp2.func_2) {
        return false;
    }
    if (fp1.stage_1 != fp2.stage_1) {
        return false;
    }
    if (fp1.stage_2 != fp2.stage_2) {
        return false;
    }
    if (fp1.var_name != fp2.var_name) {
        return false;
    }
    return true;
}

bool IRComparator::compare_parameter(const Parameter &p1, const Parameter &p2) {
    return true;
}

bool equal(const Halide::Pipeline &p1, const Halide::Pipeline &p2) {
    return IRComparator().compare_pipeline(p1, p2);
}
}  // namespace Internal
}  // namespace Halide