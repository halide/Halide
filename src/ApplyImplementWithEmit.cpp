#include "ApplyImplementWithEmit.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Error.h"
#include "Func.h"
#include "IR.h"
#include "IRMutator.h"
#include "ImplementWithMatcher.h"
#include "Instruction.h"
#include "Pipeline.h"
#include "Schedule.h"

namespace Halide {
namespace Internal {

namespace {

// Parse the matcher's full-name renames (e.g. "out.s0.i" -> "ux.s0.x")
// into bare-Var renames ("i" -> "x"). Entries that don't look like
// stage-qualified For names (e.g. handmade LetStmt bindings) are
// passed through as-is so emit callbacks can still look them up.
std::map<std::string, std::string> bare_var_renames(
    const std::map<std::string, std::string> &full_renames) {
    std::map<std::string, std::string> out;
    for (const auto &kv : full_renames) {
        const std::string &sk = kv.first;
        const std::string &uk = kv.second;
        // Look for ".s<digits>." on both sides.
        size_t s_s = sk.find(".s");
        size_t u_s = uk.find(".s");
        if (s_s == std::string::npos || u_s == std::string::npos) {
            out.try_emplace(sk, uk);
            continue;
        }
        size_t s_dot = sk.find('.', s_s + 2);
        size_t u_dot = uk.find('.', u_s + 2);
        if (s_dot == std::string::npos || u_dot == std::string::npos) {
            out.try_emplace(sk, uk);
            continue;
        }
        std::string sb = sk.substr(s_dot + 1);
        std::string ub = uk.substr(u_dot + 1);
        if (sb.empty() || ub.empty()) {
            continue;
        }
        // First-seen wins; in well-formed canonical IR a bare Var has
        // one binding per matched region.
        out.try_emplace(sb, ub);
    }
    return out;
}

// IRMutator that, when it encounters a For node whose name appears in
// `replacements`, returns the corresponding emit Stmt verbatim
// (replacing the entire For loop + body — the design contract is that
// the emit() output owns the computation of the matched region).
class EmitSubstituter : public IRMutator {
    const std::map<std::string, Stmt> &replacements;

public:
    explicit EmitSubstituter(const std::map<std::string, Stmt> &r)
        : replacements(r) {
    }

protected:
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        auto it = replacements.find(op->name);
        if (it != replacements.end()) {
            return it->second;
        }
        return IRMutator::visit(op);
    }
};

}  // namespace

Stmt apply_implement_with_emit(const Stmt &s,
                               std::map<std::string, Function> &env,
                               const Target &target) {
    if (!s.defined()) {
        return s;
    }

    struct DirectiveRef {
        std::string func_name;
        int stage;
        ImplementWithDirective directive;
    };

    std::vector<DirectiveRef> pending;
    for (auto &kv : env) {
        const Function &fn = kv.second;
        for (const ImplementWithDirective &d :
             fn.definition().schedule().implement_with_directives()) {
            pending.push_back({fn.name(), 0, d});
        }
        for (size_t i = 0; i < fn.updates().size(); ++i) {
            const StageSchedule &ss =
                fn.update(static_cast<int>(i)).schedule();
            for (const ImplementWithDirective &d : ss.implement_with_directives()) {
                pending.push_back({fn.name(), static_cast<int>(i + 1), d});
            }
        }
    }

    if (pending.empty()) {
        return s;
    }

    std::map<std::string, Stmt> replacements;

    for (const DirectiveRef &p : pending) {
        const ImplementWithDirective &d = p.directive;

        // v1 soft-failure semantics: if the user-side For at the named
        // loop level cannot be found (e.g. it was eliminated by
        // vectorize_loops, fused away, etc.), emit a warning and leave
        // the canonical-form Stmt untouched. The design ([DESIGN.md]
        // §4.3) calls for a hard error in Strict mode; v1 softens this
        // pending matcher gaps documented in the Phase 5 changelog
        // entry and DECISIONS.md "Phase 5 soft-failure semantics".
        Stmt user_loop = find_implement_with_loop(s, p.func_name, p.stage,
                                                  d.loop_var_name);
        if (!user_loop.defined()) {
            user_warning
                << "implement_with: emit substitution skipped for "
                << "instruction \"" << d.instruction.name()
                << "\" on Func \"" << p.func_name << "\" stage "
                << p.stage << " loop \"" << d.loop_var_name
                << "\": no For with that stage-qualified name found in "
                   "the canonical-form Stmt. The user schedule probably "
                   "fused, vectorized, or otherwise eliminated the named "
                   "loop level. The matched region falls through to "
                   "ordinary lowering.\n";
            continue;
        }
        const For *user_for = user_loop.as<For>();
        internal_assert(user_for)
            << "find_implement_with_loop returned a non-For Stmt.";

        Pipeline spec = d.instruction.spec();
        user_assert(spec.outputs().size() == 1 + d.co_output_names.size())
            << "implement_with: emit substitution: instruction \""
            << d.instruction.name() << "\" has " << spec.outputs().size()
            << " output Func(s) but the directive on Func \"" << p.func_name
            << "\" lists " << d.co_output_names.size() << " co-output(s) "
            << "(expected "
            << (spec.outputs().empty() ? 0 : spec.outputs().size() - 1)
            << ").\n";

        const std::string spec_out_name = spec.outputs()[0].name();
        Stmt spec_canonical = lower_spec_to_canonical_form(spec, target);
        Stmt spec_loop = find_spec_primary_loop(spec_canonical,
                                                spec_out_name, p.stage);
        if (!spec_loop.defined()) {
            user_warning
                << "implement_with: emit substitution skipped for "
                << "instruction \"" << d.instruction.name() << "\": the "
                << "spec primary's outermost For at stage " << p.stage
                << " (output \"" << spec_out_name
                << "\") was not located in canonical form. The matched "
                << "region falls through to ordinary lowering.\n";
            continue;
        }

        MatchResult mr = match_canonical_form(spec_loop, user_loop);
        if (!mr.success) {
            user_warning
                << "implement_with: structural match failed for "
                << "instruction \"" << d.instruction.name()
                << "\" on Func \"" << p.func_name << "\" at stage "
                << p.stage << " loop \"" << d.loop_var_name << "\": "
                << mr.failure_reason
                << " The matched region falls through to ordinary "
                << "lowering. (Soft-failure v1 semantics; see "
                << "DECISIONS.md.)\n";
            continue;
        }

        // Build the MatchContext from the matcher's bindings.
        auto contents = std::make_shared<MatchContextContents>();
        contents->func_rename = mr.func_rename;
        // Ensure the primary output maps to the user's primary Func even
        // if the canonical-form ProducerConsumer for the spec primary
        // was rewritten away during the prefix (the spec_out_name slot
        // may or may not show up in func_rename depending on the IR
        // shape). MatchContext::output(spec_out_name) keys off
        // user_primary_name as a fallback.
        contents->user_primary_name = p.func_name;
        contents->spec_primary_name = spec_out_name;
        contents->var_rename = bare_var_renames(mr.var_rename);
        contents->target = target;

        MatchContext ctx(contents);
        Stmt emit_stmt = d.instruction.emit(ctx);
        user_assert(emit_stmt.defined())
            << "implement_with: emit callback for instruction \""
            << d.instruction.name() << "\" returned an undefined Stmt.\n";

        // If two directives match the same For (e.g. user accidentally
        // re-applied the same directive), let the second win and warn.
        // Silently overwriting would mask the bug.
        auto [it, inserted] =
            replacements.emplace(user_for->name, std::move(emit_stmt));
        user_assert(inserted)
            << "implement_with: emit substitution: two directives "
            << "target the same canonical-form For \"" << user_for->name
            << "\". This is almost certainly an authoring mistake (the "
            << "same Func + stage + loop var was scheduled with "
            << "implement_with twice).\n";
    }

    EmitSubstituter sub(replacements);
    return sub(s);
}

}  // namespace Internal
}  // namespace Halide
