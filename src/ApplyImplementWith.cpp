#include "ApplyImplementWith.h"

#include <algorithm>
#include <vector>

#include <optional>

#include "Error.h"
#include "FindCalls.h"
#include "Func.h"
#include "IROperator.h"
#include "ImplementWithMatcher.h"
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

// Transfer storage-dim properties set by Func::align_storage and
// Func::bound_storage from `spec` onto `user`. Unlike bounds() (which
// is an append-only list), storage_dims() is a fixed-size vector
// initialized at Func definition with one entry per pure arg. So we
// mutate the existing entries in place rather than push new ones.
// Conflicting constant alignments / storage bounds error out by the
// same shape as check_bound_conflict.
void transfer_storage_dims(const Function &spec, Function &user,
                           const VarRenameMap &rename,
                           const std::string &instr_name) {
    std::vector<StorageDim> &user_dims = user.schedule().storage_dims();
    for (const StorageDim &s : spec.schedule().storage_dims()) {
        if (!s.alignment.defined() && !s.bound.defined()) {
            continue;
        }
        auto it = rename.find(s.var);
        if (it == rename.end()) {
            // The matched For binding may not cover non-pure storage
            // axes the spec set; fall back to silent skip rather than
            // hard-error here. Phase 7 (affine match parameters) is
            // the right place for richer dim-to-dim mapping.
            continue;
        }
        const std::string &user_var = it->second;
        StorageDim *user_dim = nullptr;
        for (auto &d : user_dims) {
            if (d.var == user_var) {
                user_dim = &d;
                break;
            }
        }
        if (!user_dim) {
            continue;
        }
        if (s.alignment.defined()) {
            if (user_dim->alignment.defined()) {
                int64_t a = 0, b = 0;
                if (both_const_int(user_dim->alignment, s.alignment, &a, &b) &&
                    a != b) {
                    user_error
                        << "implement_with: instruction \"" << instr_name
                        << "\" requires align_storage(extent) = " << b
                        << " on var \"" << user_var << "\" of Func \""
                        << user.name() << "\", but a prior align_storage() "
                        << "call on the same var declared alignment = " << a
                        << ". These two constraints cannot both hold.\n";
                }
            } else {
                user_dim->alignment = s.alignment;
            }
        }
        if (s.bound.defined()) {
            if (user_dim->bound.defined()) {
                int64_t a = 0, b = 0;
                if (both_const_int(user_dim->bound, s.bound, &a, &b) &&
                    a != b) {
                    user_error
                        << "implement_with: instruction \"" << instr_name
                        << "\" requires bound_storage = " << b << " on var \""
                        << user_var << "\" of Func \"" << user.name()
                        << "\", but a prior bound_storage() call declared "
                        << "= " << a
                        << ". These two constraints cannot both hold.\n";
                }
            } else {
                user_dim->bound = s.bound;
            }
        }
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

// Helper: derive a bare-var rename map (e.g. "i" -> "x") from the
// matcher's full-loop-name bindings (e.g. "out.s0.i" -> "user_out.s0.x").
// The matcher binds the full stage-qualified For-loop names; bound() and
// other Var-keyed schedule entries on the source spec/user Funcs are
// keyed by the bare Var name only.
//
// We parse each matched binding whose key has the spec-side prefix
// "<spec_func>.s<stage>." and whose value has the user-side prefix
// "<user_func>.s<stage>." (note: the stage indices on either side don't
// need to agree — implement_with maps a user-stage's loop level onto
// the spec-stage's structurally-matched loop). The remainder after the
// stage prefix is the bare Var (or split-var dotted name, e.g. "i.io").
VarRenameMap bare_var_renames_from_matcher(
    const std::map<std::string, std::string> &full_name_renames,
    const Function &spec_func,
    const Function &user_func) {
    VarRenameMap m;
    const std::string spec_prefix = spec_func.name() + ".s";
    const std::string user_prefix = user_func.name() + ".s";
    for (const auto &kv : full_name_renames) {
        const std::string &sk = kv.first;
        const std::string &uk = kv.second;
        if (sk.compare(0, spec_prefix.size(), spec_prefix) != 0 ||
            uk.compare(0, user_prefix.size(), user_prefix) != 0) {
            continue;
        }
        size_t sdot = sk.find('.', spec_prefix.size());
        size_t udot = uk.find('.', user_prefix.size());
        if (sdot == std::string::npos || udot == std::string::npos) {
            continue;
        }
        std::string sb = sk.substr(sdot + 1);
        std::string ub = uk.substr(udot + 1);
        if (sb.empty() || ub.empty()) {
            continue;
        }
        // First-seen wins (same bare Var should not have conflicting
        // bindings in well-formed canonical IR; if it does, leave the
        // first entry in place so callers see a deterministic result).
        m.emplace(sb, ub);
    }
    return m;
}

// Pre-matcher pass for a single directive: target-feature check and
// primary-output bound transfer. The primary transfer must happen
// before the matcher runs because lowering the user pipeline to
// canonical form needs the spec's bounds installed on the user primary
// --- without them, the user's outermost For has symbolic min/extent
// (out.min.0 / out.extent.0) and will not structurally match the
// spec's constant-bound For. Positional rename is sufficient here: the
// directive anchors spec-arg-0 to user-arg-0 by construction.
//
// Multi-output instructions: the directive's `co_output_names` lists
// the user-side Funcs that share the implementation with `user_primary`.
// For each co-output, the spec's corresponding output Func (matched by
// position in spec.outputs()) has its bounds transferred onto the user
// co-output. Single-output specs (Tuple-valued primaries included) take
// the same path with an empty co_output_names list.
void apply_primary_transfer(const ImplementWithDirective &d,
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

    // Co-output arity must match. spec.outputs()[0] always pairs with
    // user_primary; spec.outputs()[k] for k >= 1 pairs with the k-th
    // entry of d.co_output_names. Tuple-valued primaries (single Func,
    // multi-component output) still have spec_outputs.size() == 1 and
    // no co-output requirement.
    user_assert(spec_outputs.size() == 1 + d.co_output_names.size())
        << "implement_with: instruction \"" << instr.name()
        << "\" has " << spec_outputs.size() << " output Funcs, but the "
        << "directive on Func \"" << user_primary.name() << "\" lists "
        << d.co_output_names.size() << " co-output(s) (expected "
        << (spec_outputs.size() == 0 ? 0 : spec_outputs.size() - 1) << ").\n";

    Function spec_primary = spec_outputs[0].function();
    VarRenameMap primary_map = build_var_rename(spec_primary, user_primary);
    transfer_bounds(spec_primary, user_primary, primary_map, instr.name());
    transfer_storage_dims(spec_primary, user_primary, primary_map, instr.name());

    for (size_t k = 0; k < d.co_output_names.size(); ++k) {
        Function spec_co = spec_outputs[k + 1].function();
        const std::string &user_co_name = d.co_output_names[k];
        auto it = env.find(user_co_name);
        user_assert(it != env.end())
            << "implement_with: directive on Func \"" << user_primary.name()
            << "\" names co-output \"" << user_co_name
            << "\", but no Func with that name is reachable in the lowering "
               "environment.\n";
        VarRenameMap co_map = build_var_rename(spec_co, it->second);
        transfer_bounds(spec_co, it->second, co_map, instr.name());
        transfer_storage_dims(spec_co, it->second, co_map, instr.name());
    }
}

// Post-matcher pass for a single directive: spec-input bound transfer.
// With a successful matcher result, look up the user Func via
// func_rename (the structural correspondence) and translate bare Vars
// via the matcher's For-name bindings: this is what lets the spec
// author use input Func names that don't match the user's. Without a
// matcher result, fall back to the Phase 3 name-keyed lookup +
// positional rename.
//
// `spec` is the Pipeline the matcher used. It must be the *same*
// invocation as the one passed to match_canonical_form --- each
// d.instruction.spec() call rebuilds the spec lambda and re-runs
// Halide's process-wide Func name uniquification, so the names of
// spec_primary and its reachable inputs differ between separate
// invocations. The matcher's func_rename keys are valid only for the
// invocation it lowered.
void apply_input_transfer(const ImplementWithDirective &d,
                          const Pipeline &spec,
                          std::map<std::string, Function> &env,
                          const MatchResult *match) {
    Function spec_primary = spec.outputs()[0].function();

    std::map<std::string, Function> reachable = find_transitive_calls(spec_primary);
    for (const auto &kv : reachable) {
        const std::string &spec_input_name = kv.first;
        const Function &spec_input = kv.second;
        if (spec_input_name == spec_primary.name()) {
            continue;
        }

        std::map<std::string, Function>::iterator user_it = env.end();
        if (match && match->success) {
            auto fit = match->func_rename.find(spec_input_name);
            if (fit != match->func_rename.end()) {
                user_it = env.find(fit->second);
            }
        }
        if (user_it == env.end()) {
            user_it = env.find(spec_input_name);
        }
        if (user_it == env.end()) {
            continue;
        }
        Function &user_input = user_it->second;
        VarRenameMap input_map;
        if (match && match->success) {
            input_map =
                bare_var_renames_from_matcher(match->var_rename, spec_input,
                                              user_input);
        }
        if (input_map.empty()) {
            input_map = build_var_rename(spec_input, user_input);
        }
        transfer_bounds(spec_input, user_input, input_map,
                        d.instruction.name());
        transfer_storage_dims(spec_input, user_input, input_map,
                              d.instruction.name());
    }
}

// Per-directive matcher context: the spec Pipeline whose canonical
// form was matched and the resulting MatchResult. The Pipeline must
// be kept alive and re-used for subsequent passes (input bound
// transfer), because Halide uniquifies Func names per-invocation: the
// "a" in spec.outputs()[0]'s reachable set is "a$N" with a different
// N each time Instruction::spec() is invoked, and the matcher's
// func_rename keys are valid only for the N from the invocation that
// produced this MatchResult.
struct MatcherContext {
    Pipeline spec;
    MatchResult result;
};

// Lower the user pipeline to canonical form and, for each pending
// directive, run the structural matcher against the spec to produce a
// MatchResult plus the spec Pipeline whose Func names the matcher
// bound. Returns one entry per pending directive in input order;
// entries have `result.success == false` for directives we could not
// match against (e.g. matched For not found on either side).
//
// This deep-copies the env internally, so does not mutate `env` here.
std::vector<MatcherContext> match_pending_directives(
    const std::vector<Function> &outputs,
    const Target &target,
    const std::vector<std::pair<std::string, ImplementWithDirective>> &pending,
    const std::vector<int> &pending_stages) {
    std::vector<MatcherContext> results(pending.size());
    if (pending.empty() || outputs.empty()) {
        return results;
    }

    // Build a Pipeline over the user's output Funcs and lower to
    // canonical form. lower_pipeline_to_canonical_form internally
    // deep-copies, so this does not see (or apply) the in-progress
    // bounds transfer that the caller's primary-output positional
    // pre-pass has installed --- but the user's pristine bounds are
    // preserved.
    std::vector<Func> output_funcs;
    output_funcs.reserve(outputs.size());
    for (const Function &f : outputs) {
        output_funcs.emplace_back(f);
    }
    Pipeline user_pipeline(output_funcs);
    Stmt user_canonical;
    try {
        user_canonical = lower_pipeline_to_canonical_form(user_pipeline, target);
    } catch (...) {
        // If the user pipeline cannot itself be lowered (e.g. some
        // structural problem the matcher would diagnose anyway), fall
        // through to the name-keyed fallback path.
        return results;
    }

    for (size_t i = 0; i < pending.size(); ++i) {
        const std::string &func_name = pending[i].first;
        const ImplementWithDirective &d = pending[i].second;
        const int stage = pending_stages[i];

        Stmt user_loop = find_implement_with_loop(user_canonical, func_name,
                                                  stage, d.loop_var_name);
        if (!user_loop.defined()) {
            continue;
        }

        Pipeline spec = d.instruction.spec();
        if (spec.outputs().size() != 1) {
            continue;
        }
        const std::string spec_out_name = spec.outputs()[0].name();
        Stmt spec_canonical = lower_spec_to_canonical_form(spec, target);
        Stmt spec_loop = find_spec_primary_loop(spec_canonical, spec_out_name,
                                                stage);
        if (!spec_loop.defined()) {
            continue;
        }

        results[i].spec = spec;
        results[i].result = match_canonical_form(spec_loop, user_loop);
    }
    return results;
}

}  // namespace

void apply_implement_with_directives(std::map<std::string, Function> &env,
                                     const std::vector<Function> &outputs,
                                     const Target &target) {
    // Collect (func_name, directive, stage_index) snapshots first so the
    // process loop can freely mutate per-function schedules without
    // tripping iterator invalidation on the directive vectors. (We are
    // not inserting or removing env entries here.)
    std::vector<std::pair<std::string, ImplementWithDirective>> pending;
    std::vector<int> pending_stages;

    for (auto &kv : env) {
        const Function &fn = kv.second;
        for (const ImplementWithDirective &d :
             fn.definition().schedule().implement_with_directives()) {
            pending.emplace_back(fn.name(), d);
            pending_stages.push_back(0);
        }
        for (size_t i = 0; i < fn.updates().size(); ++i) {
            const StageSchedule &s = fn.update(static_cast<int>(i)).schedule();
            for (const ImplementWithDirective &d : s.implement_with_directives()) {
                pending.emplace_back(fn.name(), d);
                pending_stages.push_back(static_cast<int>(i + 1));
            }
        }
    }

    if (pending.empty()) {
        return;
    }

    // First pass: target-feature check + primary-output bound transfer
    // for every directive (positional rename). This installs the
    // spec's constant bounds on the user's primary so that, when we
    // lower the user pipeline to canonical form below, its outermost
    // For has constants matching the spec's (instead of symbolic
    // out.min.0 / out.extent.0 that would foil structural matching).
    for (size_t i = 0; i < pending.size(); ++i) {
        auto it = env.find(pending[i].first);
        internal_assert(it != env.end())
            << "implement_with: pending directive references Func \""
            << pending[i].first << "\" which is not in the lowering env.\n";
        apply_primary_transfer(pending[i].second, it->second, env, target);
    }

    // Second pass: run the matcher on each directive against the
    // user-side IR (now lowered with primary bounds installed). The
    // matcher pass is opportunistic --- any directive whose user-side
    // For we cannot lower or locate falls back to the Phase 3
    // name-keyed lookup + positional rename for input bound transfer.
    std::vector<MatcherContext> match_ctxs =
        match_pending_directives(outputs, target, pending, pending_stages);

    // Third pass: spec-input bound transfer, using matcher results
    // when available. We use the spec Pipeline kept alive in
    // match_ctxs (not a fresh d.instruction.spec() call) because
    // Halide uniquifies Func names per invocation: the matcher's
    // func_rename keys reference the invocation it lowered. For
    // directives the matcher pass skipped (spec is default-
    // constructed), invoke spec() now so the fallback name-keyed path
    // still has a Pipeline to walk.
    for (size_t i = 0; i < pending.size(); ++i) {
        if (!match_ctxs[i].spec.defined()) {
            match_ctxs[i].spec = pending[i].second.instruction.spec();
        }
        const MatchResult *m = match_ctxs[i].result.success
                                   ? &match_ctxs[i].result
                                   : nullptr;
        apply_input_transfer(pending[i].second, match_ctxs[i].spec, env, m);
    }
}

}  // namespace Internal
}  // namespace Halide
