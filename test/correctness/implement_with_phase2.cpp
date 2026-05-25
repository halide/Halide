// Phase 2 test for the `implement_with` feature. See
// docs/implement_with/DESIGN.md (§4.7, §8.2 Phase 2) and
// docs/implement_with/IMPLEMENTATION_STATUS.md.
//
// Exercises spec-pattern Func mode:
//   - Spec thunks may reference undefined input Funcs (match-pattern syntax).
//   - Output Funcs in the spec Pipeline are marked spec-pattern by spec().
//   - Realizing or compiling a spec-pattern Pipeline is a hard error.
//   - Scheduling directives on spec-pattern Funcs are accepted without error.

#include "Halide.h"

#include <cstdio>

using namespace Halide;

namespace {

Internal::Stmt stub_emit(const MatchContext &) {
    return Internal::Evaluate::make(Expr(0));
}

// Build an Instruction whose spec uses the design-doc pattern:
// out(i) = a(i) * b(i) + c(i)  with a, b, c undefined.
Instruction make_vfmadd_style() {
    return Instruction::declare("vfmadd_style")
        .spec([]() -> Pipeline {
            // Input Funcs declare their type but have no body. They are
            // auto-stubbed when called inside out's definition (Phase 2).
            Var i;
            Func a(Float(32), 1, "a"), b(Float(32), 1, "b"),
                c(Float(32), 1, "c"), out("out");
            out(i) = a(i) * b(i) + c(i);
            // Contractual scheduling directives. Now that a/b/c are
            // auto-stubbed, they have 'i' in their args and accept directives.
            for (Func f : std::vector<Func>{a, b, c, out}) {
                f.bound(i, 0, 8).vectorize(i, 8);
            }
            return Pipeline({out});
        })
        .require({Target::FMA, Target::AVX2})
        .emit(stub_emit)
        .build();
}

void test_spec_thunk_callable_with_undefined_inputs() {
    // The spec thunk references undefined Funcs a, b, c. Calling
    // Instruction::spec() must succeed — undefined input Funcs are
    // legal in a spec context (they form the match pattern).
    Instruction instr = make_vfmadd_style();
    Pipeline p = instr.spec();

    if (p.outputs().size() != 1) {
        printf("Spec pipeline should have one output, got %zu\n",
               p.outputs().size());
        std::exit(1);
    }
    if (p.outputs()[0].name() != "out") {
        printf("Spec output should be named \"out\", got \"%s\"\n",
               p.outputs()[0].name().c_str());
        std::exit(1);
    }
}

void test_spec_pipeline_output_is_marked_spec_pattern() {
    Instruction instr = make_vfmadd_style();
    Pipeline p = instr.spec();

    for (const Func &f : p.outputs()) {
        if (!f.function().is_spec_pattern()) {
            printf("Output Func \"%s\" should be marked spec-pattern after spec()\n",
                   f.name().c_str());
            std::exit(1);
        }
    }
}

void test_spec_pipeline_cannot_be_realized() {
    // Realizing a spec-pattern Pipeline must be a hard error. Test
    // only when the Halide library was compiled with exceptions.
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        printf("[SKIP] test_spec_pipeline_cannot_be_realized: "
               "exceptions disabled at runtime\n");
        return;
    }

    Instruction instr = make_vfmadd_style();
    Pipeline p = instr.spec();

    bool got_error = false;
    try {
        p.realize({8});
    } catch (const Halide::CompileError &e) {
        got_error = true;
        // The error message should mention spec-pattern.
        std::string msg(e.what());
        if (msg.find("spec-pattern") == std::string::npos) {
            printf("Expected error to mention \"spec-pattern\", got:\n%s\n",
                   msg.c_str());
            std::exit(1);
        }
    }
    if (!got_error) {
        printf("Realizing a spec-pattern Pipeline should have errored, but didn't.\n");
        std::exit(1);
    }
#else
    printf("[SKIP] test_spec_pipeline_cannot_be_realized: "
           "built without exception support\n");
#endif
}

void test_scheduling_directives_on_spec_funcs_accepted() {
    // Scheduling directives applied to spec-pattern Funcs (inside the
    // spec thunk) must not error. The make_vfmadd_style() instruction
    // already exercises this (bound + vectorize); calling spec() runs
    // the thunk and the pipeline is returned without error.
    Instruction instr = make_vfmadd_style();
    Pipeline p = instr.spec();  // would throw on scheduling error

    if (p.outputs().empty()) {
        printf("Spec pipeline unexpectedly empty after scheduling directives.\n");
        std::exit(1);
    }
}

void test_each_spec_call_returns_fresh_marked_pipeline() {
    // spec() is called multiple times (e.g., by matcher + schedule
    // transfer). Each call should return a freshly-constructed Pipeline
    // with newly-marked output Funcs.
    Instruction instr = make_vfmadd_style();
    Pipeline p1 = instr.spec();
    Pipeline p2 = instr.spec();

    // Outputs are different Func objects (fresh each call).
    Internal::Function out1 = p1.outputs()[0].function();
    Internal::Function out2 = p2.outputs()[0].function();
    if (out1.same_as(out2)) {
        printf("Two spec() calls returned the same underlying Function object.\n");
        std::exit(1);
    }
    // Both should be marked spec-pattern.
    if (!out1.is_spec_pattern() || !out2.is_spec_pattern()) {
        printf("One of the repeated spec() calls did not mark output as spec-pattern.\n");
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    test_spec_thunk_callable_with_undefined_inputs();
    test_spec_pipeline_output_is_marked_spec_pattern();
    test_spec_pipeline_cannot_be_realized();
    test_scheduling_directives_on_spec_funcs_accepted();
    test_each_spec_call_returns_fresh_marked_pipeline();

    printf("Success!\n");
    return 0;
}
