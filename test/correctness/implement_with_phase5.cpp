// Phase 5 test for the `implement_with` feature. See
// docs/implement_with/DESIGN.md §4.3, §8.2 Phase 5, and
// docs/implement_with/IMPLEMENTATION_STATUS.md.
//
// Exercises the emit substitution: after the canonical-form prefix runs,
// each implement_with directive has its matched For region replaced by
// the Instruction's emit() Stmt. The test runs the full compile path
// (`pipe.compile_to_module`) and inspects the resulting LoweredFunc
// bodies for sentinel markers produced by the emit callback.

#include "Halide.h"

#include "IRVisitor.h"

#include <cstdio>
#include <string>
#include <utility>

using namespace Halide;

namespace {

const char *const kSentinelName = "halide_phase5_emit_sentinel";

// Emit callback: returns an Evaluate of a Call::Extern named
// kSentinelName, with an arg that includes the user-side output Func
// name resolved through MatchContext::output. The arg lets the test
// confirm the match binding survived all the way to the substituted
// Stmt.
Internal::Stmt sentinel_emit(const MatchContext &ctx) {
    Expr output_name_str =
        Internal::StringImm::make(ctx.output("out"));
    Expr input_a_str = Internal::StringImm::make(ctx.input("a"));
    return Internal::Evaluate::make(Internal::Call::make(
        Int(32), kSentinelName,
        {output_name_str, input_a_str},
        Internal::Call::Extern));
}

Instruction make_fma_sentinel_instruction() {
    return Instruction::declare("fma_sentinel")
        .spec([]() -> Pipeline {
            Var i("i");
            Func a(Float(32), 1, "a"), b(Float(32), 1, "b"),
                c(Float(32), 1, "c"), out("out");
            out(i) = a(i) * b(i) + c(i);
            for (Func f : std::vector<Func>{a, b, c, out}) {
                f.bound(i, 0, 8);
            }
            return Pipeline({out});
        })
        .require({})
        .emit(sentinel_emit)
        .build();
}

Target host_target() {
    return get_jit_target_from_environment();
}

// IRVisitor that records whether a Call::Extern with a given name was
// found, and (separately) any StringImm operands of that Call.
class FindSentinelCall : public Internal::IRVisitor {
public:
    const std::string target;
    bool found = false;
    std::vector<std::string> string_args;

    explicit FindSentinelCall(std::string t)
        : target(std::move(t)) {
    }

    using Internal::IRVisitor::visit;

    void visit(const Internal::Call *op) override {
        if (op->name == target) {
            found = true;
            for (const Expr &e : op->args) {
                if (const Internal::StringImm *s = e.as<Internal::StringImm>()) {
                    string_args.push_back(s->value);
                }
            }
        }
        Internal::IRVisitor::visit(op);
    }
};

bool stmt_contains_sentinel(const Internal::Stmt &s,
                            std::vector<std::string> *string_args = nullptr) {
    FindSentinelCall v(kSentinelName);
    if (s.defined()) {
        s.accept(&v);
    }
    if (v.found && string_args) {
        *string_args = std::move(v.string_args);
    }
    return v.found;
}

bool module_contains_sentinel(const Module &m,
                              std::vector<std::string> *string_args = nullptr) {
    for (const auto &lf : m.functions()) {
        std::vector<std::string> tmp;
        if (stmt_contains_sentinel(lf.body, &tmp)) {
            if (string_args) *string_args = std::move(tmp);
            return true;
        }
    }
    return false;
}

// Smoke test: emit Stmt is present in the lowered Module, with the
// MatchContext-resolved user buffer name(s) propagated as string args.
//
// Uses ImageParam inputs with pinned mins (the same pattern as the
// Phase 4 vfmadd case study), because compute_root'd inline Funcs on
// the user side produce slightly different canonical-form indices
// than the spec's auto-stubbed inputs, foiling structural match in v1.
// Documented in DECISIONS.md "Phase 5 soft-failure semantics".
void test_emit_sentinel_survives_lowering() {
    Instruction instr = make_fma_sentinel_instruction();
    Pipeline spec = instr.spec();
    Target t = host_target();

    Var x("x");
    ImageParam ai(Float(32), 1, "a");
    ImageParam bi(Float(32), 1, "b");
    ImageParam ci(Float(32), 1, "c");
    ai.dim(0).set_bounds(0, 8);
    bi.dim(0).set_bounds(0, 8);
    ci.dim(0).set_bounds(0, 8);
    Func out("out_phase5_smoke");
    out(x) = ai(x) * bi(x) + ci(x);
    // Do not call out.bound(x, 0, 8): the spec's bound transfers in
    // apply_implement_with_directives, and a duplicate (user + transferred)
    // bound entry can prevent allocation_bounds_inference from pinning
    // the output's storage_min to 0, leaving the Store index symbolic.
    out.implement_with(x, instr);

    Pipeline pipe(out);
    Module m = pipe.compile_to_module({ai, bi, ci}, "test_phase5_smoke", t);

    std::vector<std::string> string_args;
    if (!module_contains_sentinel(m, &string_args)) {
        fprintf(stderr,
                "test_emit_sentinel_survives_lowering: sentinel call \"%s\" "
                "not found in compiled Module. The emit substitution either "
                "did not run, or the matched For was eliminated before the "
                "substitution pass.\n",
                kSentinelName);
        std::exit(1);
    }

    bool saw_out = false, saw_a = false;
    for (const std::string &s : string_args) {
        if (s == "out_phase5_smoke") saw_out = true;
        if (s == "a") saw_a = true;
    }
    if (!saw_out) {
        fprintf(stderr,
                "test_emit_sentinel_survives_lowering: expected the "
                "MatchContext::output(\"out\") binding to resolve to "
                "user Func \"out_phase5_smoke\"; was not seen in the "
                "sentinel Call args.\n");
        for (const std::string &s : string_args) {
            fprintf(stderr, "  arg: %s\n", s.c_str());
        }
        std::exit(1);
    }
    if (!saw_a) {
        fprintf(stderr,
                "test_emit_sentinel_survives_lowering: expected the "
                "MatchContext::input(\"a\") binding to resolve to user "
                "ImageParam \"a\" (same name, routed through the "
                "matcher's func_rename); was not seen in the sentinel "
                "Call args.\n");
        for (const std::string &s : string_args) {
            fprintf(stderr, "  arg: %s\n", s.c_str());
        }
        std::exit(1);
    }
}

// Without any implement_with directive, the emit-substitution pass
// should be a no-op (no warnings, normal compilation).
void test_no_directive_compiles_cleanly() {
    Var x("x");
    Func out("out_no_directive");
    out(x) = 0.0f;
    out.bound(x, 0, 8);

    Pipeline pipe(out);
    Module m = pipe.compile_to_module({}, "test_phase5_no_directive",
                                      host_target());
    if (module_contains_sentinel(m)) {
        fprintf(stderr,
                "test_no_directive_compiles_cleanly: unexpectedly found a "
                "sentinel call in the lowered Module of a pipeline without "
                "any implement_with directives.\n");
        std::exit(1);
    }
}

// The matched For (the entire loop, not just its body) is replaced
// with the emit() output. Confirm by checking that the user's loop
// var name no longer appears as a For loop in the substituted region.
class FindForByName : public Internal::IRVisitor {
public:
    const std::string target;
    bool found = false;
    explicit FindForByName(std::string n) : target(std::move(n)) {
    }
    using Internal::IRVisitor::visit;
    void visit(const Internal::For *op) override {
        if (op->name == target) {
            found = true;
            return;
        }
        Internal::IRVisitor::visit(op);
    }
};

void test_matched_for_is_replaced() {
    Instruction instr = make_fma_sentinel_instruction();

    Var x("x");
    ImageParam ai(Float(32), 1, "a");
    ImageParam bi(Float(32), 1, "b");
    ImageParam ci(Float(32), 1, "c");
    ai.dim(0).set_bounds(0, 8);
    bi.dim(0).set_bounds(0, 8);
    ci.dim(0).set_bounds(0, 8);
    Func out("out_phase5_for_replaced");
    out(x) = ai(x) * bi(x) + ci(x);
    // See test_emit_sentinel_survives_lowering: do not call out.bound()
    // alongside the directive --- duplicate bounds prevent the
    // output's storage_min from being pinned.
    out.implement_with(x, instr);

    Pipeline pipe(out);
    Module m = pipe.compile_to_module({ai, bi, ci},
                                      "test_phase5_for_replaced",
                                      host_target());

    // The original matched For was named "out_phase5_for_replaced.s0.x".
    // After Phase 5 substitution, that exact For node should be gone --
    // its position is now occupied by the emit Stmt (an Evaluate of the
    // sentinel call).
    std::string old_for_name = "out_phase5_for_replaced.s0.x";
    for (const auto &lf : m.functions()) {
        FindForByName f(old_for_name);
        if (lf.body.defined()) lf.body.accept(&f);
        if (f.found) {
            fprintf(stderr,
                    "test_matched_for_is_replaced: For \"%s\" still "
                    "exists in the lowered Module; emit substitution "
                    "did not replace it.\n",
                    old_for_name.c_str());
            std::exit(1);
        }
    }
}

// MatchContext::var resolves bare spec Var names to user-side Var
// names via the matcher's For-loop renames. Verify by emitting a
// sentinel that consumes ctx.var("i") as a StringImm arg.
Internal::Stmt sentinel_var_emit(const MatchContext &ctx) {
    Expr var_name_str = Internal::StringImm::make(ctx.var("i"));
    return Internal::Evaluate::make(Internal::Call::make(
        Int(32), kSentinelName, {var_name_str},
        Internal::Call::Extern));
}

void test_match_context_var_lookup() {
    Instruction instr = Instruction::declare("fma_var_sentinel")
                            .spec([]() -> Pipeline {
                                Var i("i");
                                Func a(Float(32), 1, "a"), b(Float(32), 1, "b"),
                                    c(Float(32), 1, "c"), out("out");
                                out(i) = a(i) * b(i) + c(i);
                                for (Func f : std::vector<Func>{a, b, c, out}) {
                                    f.bound(i, 0, 8);
                                }
                                return Pipeline({out});
                            })
                            .require({})
                            .emit(sentinel_var_emit)
                            .build();

    Var x("a_strange_user_var_name");
    ImageParam ai(Float(32), 1, "a");
    ImageParam bi(Float(32), 1, "b");
    ImageParam ci(Float(32), 1, "c");
    ai.dim(0).set_bounds(0, 8);
    bi.dim(0).set_bounds(0, 8);
    ci.dim(0).set_bounds(0, 8);
    Func out("out_phase5_var_lookup");
    out(x) = ai(x) * bi(x) + ci(x);
    out.implement_with(x, instr);

    Pipeline pipe(out);
    Module m = pipe.compile_to_module({ai, bi, ci},
                                      "test_phase5_var_lookup",
                                      host_target());

    std::vector<std::string> string_args;
    if (!module_contains_sentinel(m, &string_args)) {
        fprintf(stderr,
                "test_match_context_var_lookup: sentinel call not found "
                "in lowered Module\n");
        std::exit(1);
    }
    bool saw_user_var = false;
    for (const std::string &s : string_args) {
        if (s == "a_strange_user_var_name") saw_user_var = true;
    }
    if (!saw_user_var) {
        fprintf(stderr,
                "test_match_context_var_lookup: MatchContext::var(\"i\") "
                "did not resolve to user var \"a_strange_user_var_name\".\n");
        for (const std::string &s : string_args) {
            fprintf(stderr, "  arg: %s\n", s.c_str());
        }
        std::exit(1);
    }
}

}  // namespace

int main(int, char **) {
    test_no_directive_compiles_cleanly();
    test_emit_sentinel_survives_lowering();
    test_matched_for_is_replaced();
    test_match_context_var_lookup();
    printf("Success!\n");
    return 0;
}
