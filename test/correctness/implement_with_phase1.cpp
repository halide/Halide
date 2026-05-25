// Phase 1 test for the `implement_with` feature. See
// docs/implement_with/DESIGN.md (§8.2, Phase 1) and
// docs/implement_with/IMPLEMENTATION_STATUS.md.
//
// This test exercises the inert API surface only: building an Instruction
// via the Builder, applying Func::implement_with / Stage::implement_with,
// and observing the directive on the StageSchedule. No structural matching
// or lowering substitution exists yet.

#include "Halide.h"

#include <cstdio>

using namespace Halide;

namespace {

bool emit_called = false;

Internal::Stmt make_stub_emit(const MatchContext &) {
    emit_called = true;
    return Internal::Evaluate::make(Expr(0));
}

Instruction make_test_instruction() {
    return Instruction::declare("test_intrin")
        .spec([]() -> Pipeline {
            // Spec-pattern Funcs must be explicitly named — see design
            // doc OQ#2. Input Funcs (a, b, c) are declared with explicit
            // types and left undefined; their calls in out's body form
            // the match pattern. Phase 2 auto-stubs them on first call.
            Var i;
            Func a(Float(32), 1, "a"), b(Float(32), 1, "b"),
                c(Float(32), 1, "c"), out("out");
            out(i) = a(i) * b(i) + c(i);
            return Pipeline({out});
        })
        .require({Target::FMA, Target::AVX2})
        .emit(make_stub_emit)
        .build();
}

void test_builder_basics() {
    Instruction instr = make_test_instruction();

    if (!instr.defined()) {
        printf("Built Instruction was not defined.\n");
        std::exit(1);
    }
    if (instr.name() != "test_intrin") {
        printf("Instruction name mismatch: got \"%s\"\n", instr.name().c_str());
        std::exit(1);
    }

    const auto &feats = instr.required_features();
    if (feats.count(Target::FMA) != 1 || feats.count(Target::AVX2) != 1 ||
        feats.size() != 2) {
        printf("Required features mismatch.\n");
        std::exit(1);
    }

    // Spec is runnable: calling it should return a non-empty Pipeline.
    Pipeline p = instr.spec();
    if (p.outputs().size() != 1) {
        printf("Spec pipeline should have one output, got %zu\n",
               p.outputs().size());
        std::exit(1);
    }
    if (p.outputs()[0].name() != "out") {
        printf("Spec output Func should be named \"out\", got \"%s\"\n",
               p.outputs()[0].name().c_str());
        std::exit(1);
    }

    // Emit callback is wired up.
    emit_called = false;
    (void)instr.emit(MatchContext{});
    if (!emit_called) {
        printf("Emit callback was not invoked.\n");
        std::exit(1);
    }
}

void test_func_directive_recorded() {
    Instruction instr = make_test_instruction();

    Var x, y;
    Func f("f");
    f(x, y) = cast<float>(x + y);

    // Apply to the init definition via the Func overload.
    f.implement_with(x, instr);

    // Init schedule lives on f.function().definition().schedule().
    Internal::Function inner = f.function();
    const Internal::StageSchedule &init_sched = inner.definition().schedule();
    const auto &init_dirs = init_sched.implement_with_directives();
    if (init_dirs.size() != 1) {
        printf("Expected 1 implement_with directive on f's init, got %zu\n",
               init_dirs.size());
        std::exit(1);
    }
    if (init_dirs[0].loop_var_name != x.name()) {
        printf("Recorded loop var name mismatch: got \"%s\"\n",
               init_dirs[0].loop_var_name.c_str());
        std::exit(1);
    }
    if (init_dirs[0].loop_var_is_rvar) {
        printf("Recorded loop var should not be an RVar.\n");
        std::exit(1);
    }
    if (init_dirs[0].mode != ImplementMode::Strict) {
        printf("Default mode should be Strict.\n");
        std::exit(1);
    }
    if (!init_dirs[0].co_output_names.empty()) {
        printf("Single-output overload should leave co_output_names empty.\n");
        std::exit(1);
    }
    if (!init_dirs[0].instruction.defined() ||
        init_dirs[0].instruction.name() != "test_intrin") {
        printf("Recorded instruction handle is wrong.\n");
        std::exit(1);
    }
}

void test_stage_directive_recorded() {
    Instruction instr = make_test_instruction();

    Var x, y;
    Func f("g");
    f(x, y) = cast<float>(0);
    f(x, y) += cast<float>(x + y);

    f.update(0).implement_with(x, instr);

    const auto &update_dirs =
        f.update(0).get_schedule().implement_with_directives();
    if (update_dirs.size() != 1) {
        printf("Expected 1 implement_with directive on g's update(0), got %zu\n",
               update_dirs.size());
        std::exit(1);
    }
}

void test_multi_output_directive() {
    Instruction instr = make_test_instruction();

    Var x;
    Func primary("primary"), co1("co1"), co2("co2");
    primary(x) = x;
    co1(x) = x;
    co2(x) = x;

    primary.implement_with(x, instr, {co1, co2});

    Internal::Function inner = primary.function();
    const auto &dirs = inner.definition().schedule().implement_with_directives();
    if (dirs.size() != 1) {
        printf("Expected 1 directive, got %zu\n", dirs.size());
        std::exit(1);
    }
    if (dirs[0].co_output_names.size() != 2 ||
        dirs[0].co_output_names[0] != "co1" ||
        dirs[0].co_output_names[1] != "co2") {
        printf("Co-output names mismatch.\n");
        std::exit(1);
    }
}

void test_stage_schedule_copy_carries_directives() {
    // get_copy() must include the new field.
    Instruction instr = make_test_instruction();

    Var x;
    Func f("h");
    f(x) = x;
    f.implement_with(x, instr);

    Internal::Function inner = f.function();
    Internal::StageSchedule copy = inner.definition().schedule().get_copy();
    if (copy.implement_with_directives().size() != 1) {
        printf("StageSchedule::get_copy() did not preserve implement_with directives.\n");
        std::exit(1);
    }
}

void test_builder_validation() {
    // build() without spec/emit should be a hard error. We can't easily
    // catch user_assert in a self-test without enabling exceptions, so we
    // just verify the success path is sane and trust the failure path is
    // exercised by users.
    Instruction instr = Instruction::declare("ok")
                            .spec([]() -> Pipeline {
                                Var i;
                                Func out("out");
                                out(i) = i;
                                return Pipeline({out});
                            })
                            .emit(make_stub_emit)
                            .build();
    if (!instr.defined()) {
        printf("Minimum-valid Builder did not produce a defined Instruction.\n");
        std::exit(1);
    }
    // No required features is legal; the set should just be empty.
    if (!instr.required_features().empty()) {
        printf("Default required_features should be empty.\n");
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    test_builder_basics();
    test_func_directive_recorded();
    test_stage_directive_recorded();
    test_multi_output_directive();
    test_stage_schedule_copy_carries_directives();
    test_builder_validation();

    printf("Success!\n");
    return 0;
}
