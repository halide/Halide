#include "ApplyImplementWith.h"

#include <algorithm>
#include <vector>

#include <optional>

#include "Error.h"
#include "FindCalls.h"
#include "Func.h"
#include "IROperator.h"
#include "Instruction.h"
#include "Pipeline.h"
#include "Schedule.h"

namespace Halide {
namespace Internal {

namespace {

// Positional spec-arg -> user-arg map. The spec author writes Vars like
// "i" in the spec body; the user has Vars like "x" on the matched Func.
// Phase 3 anchors the mapping by argument position (the structural
// matcher in Phase 4 will refine this).
using VarRenameMap = std::map<std::string, std::string>;

VarRenameMap build_var_rename(const Function &spec, const Function &user) {
    VarRenameMap m;
    const std::vector<std::string> &spec_args = spec.args();
    const std::vector<std::string> &user_args = user.args();
    const size_t n = std::min(spec_args.size(), user_args.size());
    for (size_t i = 0; i < n; ++i) {
        m[spec_args[i]] = user_args[i];
    }
    return m;
}

// If both `a` and `b` are constant integer Exprs, return true and write the
// values to `va`/`vb`. Otherwise return false. Symbolic bounds (e.g. match
// parameters in v1.5) flow through unchecked and let bounds inference
// catch any inconsistency.
bool both_const_int(const Expr &a, const Expr &b, int64_t *va, int64_t *vb) {
    std::optional<int64_t> pa = as_const_int(a);
    std::optional<int64_t> pb = as_const_int(b);
    if (!pa || !pb) {
        return false;
    }
    *va = *pa;
    *vb = *pb;
    return true;
}

void check_bound_conflict(const Bound &existing, const Bound &incoming,
                          const Function &user, const std::string &instr_name) {
    int64_t a = 0, b = 0;
    if (existing.min.defined() && incoming.min.defined() &&
        both_const_int(existing.min, incoming.min, &a, &b) && a != b) {
        user_error << "implement_with: instruction \"" << instr_name
                   << "\" requires bound(min) = " << b << " on var \""
                   << incoming.var << "\" of Func \"" << user.name()
                   << "\", but a prior bound() call on the same var declared min = "
                   << a << ". These two constraints cannot both hold.\n";
    }
    if (existing.extent.defined() && incoming.extent.defined() &&
        both_const_int(existing.extent, incoming.extent, &a, &b) && a != b) {
        user_error << "implement_with: instruction \"" << instr_name
                   << "\" requires bound(extent) = " << b << " on var \""
                   << incoming.var << "\" of Func \"" << user.name()
                   << "\", but a prior bound() call on the same var declared extent = "
                   << a << ". These two constraints cannot both hold.\n";
    }
}

void transfer_bounds(const Function &spec, Function &user,
                     const VarRenameMap &rename,
                     const std::string &instr_name) {
    for (const Bound &b : spec.schedule().bounds()) {
        auto it = rename.find(b.var);
        user_assert(it != rename.end())
            << "implement_with: instruction \"" << instr_name
            << "\": spec Func \"" << spec.name() << "\" carries a bound on "
            << "var \"" << b.var << "\", but that var has no positional "
            << "counterpart in user Func \"" << user.name()
            << "\" (which has " << user.args().size() << " arg(s)).\n";
        Bound translated = b;
        translated.var = it->second;

        for (const Bound &existing : user.schedule().bounds()) {
            if (existing.var == translated.var) {
                check_bound_conflict(existing, translated, user, instr_name);
            }
        }
        user.schedule().bounds().push_back(translated);
    }
}

void check_target_features(const Instruction &instr, const Target &target,
                           const std::string &user_func_name) {
    for (Target::Feature f : instr.required_features()) {
        user_assert(target.has_feature(f))
            << "implement_with: instruction \"" << instr.name()
            << "\" applied to Func \"" << user_func_name
            << "\" requires Target feature \"" << Target::feature_to_name(f)
            << "\", which is not enabled in the compile target \""
            << target.to_string() << "\".\n";
    }
}

void process_directive(const ImplementWithDirective &d,
                       Function &user_primary,
                       std::map<std::string, Function> &env,
                       const Target &target) {
    const Instruction &instr = d.instruction;
    internal_assert(instr.defined())
        << "implement_with: directive on Func \"" << user_primary.name()
        << "\" has an undefined Instruction handle. This should have been "
        << "caught at Stage::implement_with call time.\n";

    check_target_features(instr, target, user_primary.name());

    Pipeline spec = instr.spec();
    const std::vector<Func> &spec_outputs = spec.outputs();

    // Phase 3 supports single-output instructions only. Multi-output
    // (co_outputs) requires a name-keyed mapping that is more naturally
    // landed alongside the matcher in Phase 4.
    user_assert(spec_outputs.size() == 1 && d.co_output_names.empty())
        << "implement_with: multi-output instructions are not yet supported. "
        << "Instruction \"" << instr.name() << "\" has " << spec_outputs.size()
        << " outputs and the directive on Func \"" << user_primary.name()
        << "\" lists " << d.co_output_names.size() << " co-outputs. "
        << "Single-output instructions only are supported in this build.\n";

    // Primary output: transfer bounds from spec output -> user primary.
    Function spec_primary = spec_outputs[0].function();
    VarRenameMap primary_map = build_var_rename(spec_primary, user_primary);
    transfer_bounds(spec_primary, user_primary, primary_map, instr.name());

    // Spec inputs are everything transitively reachable from the spec
    // primary, minus the primary itself. For each, look up the user Func
    // with the same name in env and transfer its bounds. If no such user
    // Func exists, skip silently — the structural matcher (Phase 4) will
    // diagnose the mismatch.
    std::map<std::string, Function> reachable = find_transitive_calls(spec_primary);
    for (const auto &kv : reachable) {
        const std::string &spec_input_name = kv.first;
        const Function &spec_input = kv.second;
        if (spec_input_name == spec_primary.name()) {
            continue;
        }
        auto user_it = env.find(spec_input_name);
        if (user_it == env.end()) {
            continue;
        }
        Function &user_input = user_it->second;
        VarRenameMap input_map = build_var_rename(spec_input, user_input);
        transfer_bounds(spec_input, user_input, input_map, instr.name());
    }
}

}  // namespace

void apply_implement_with_directives(std::map<std::string, Function> &env,
                                     const Target &target) {
    // Collect a snapshot of (function, directive) pairs first so the
    // process loop can freely mutate per-function schedules without
    // tripping iterator invalidation on the directive vectors. (We are
    // not inserting or removing env entries here.)
    struct Pending {
        std::string func_name;
        ImplementWithDirective directive;
    };
    std::vector<Pending> pending;

    for (auto &kv : env) {
        const Function &fn = kv.second;
        for (const ImplementWithDirective &d :
             fn.definition().schedule().implement_with_directives()) {
            pending.push_back({fn.name(), d});
        }
        for (size_t i = 0; i < fn.updates().size(); ++i) {
            const StageSchedule &s = fn.update(static_cast<int>(i)).schedule();
            for (const ImplementWithDirective &d : s.implement_with_directives()) {
                pending.push_back({fn.name(), d});
            }
        }
    }

    for (const Pending &p : pending) {
        auto it = env.find(p.func_name);
        internal_assert(it != env.end())
            << "implement_with: pending directive references Func \""
            << p.func_name << "\" which is not in the lowering env.\n";
        process_directive(p.directive, it->second, env, target);
    }
}

}  // namespace Internal
}  // namespace Halide
