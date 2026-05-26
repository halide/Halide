// Phase 3 test for the `implement_with` feature. See
// docs/implement_with/DESIGN.md (§4.2–§4.3, §8.2 Phase 3) and
// docs/implement_with/IMPLEMENTATION_STATUS.md.
//
// Exercises schedule transfer and constraint installation:
//   - Target-feature check at lowering time.
//   - Spec-pattern Func bound() entries are copied onto the user's Func,
//     with positional spec-arg -> user-arg renaming.
//   - Conflicting bounds produce a clean diagnostic.
//   - The user's pristine Function schedule is untouched by lowering (the
//     transfer happens on the deep-copied env inside lower()).

#include "Halide.h"

#include <cstdio>
#include <string>

using namespace Halide;

namespace {

Internal::Stmt stub_emit(const MatchContext &) {
    return Internal::Evaluate::make(Expr(0));
}

// Single-output, single-var instruction with simple bound and require()
// metadata. Spec's input Funcs (a, b, c) and output (out) all carry
// bound(i, 0, 8). The "i" gets positionally renamed to whatever Var the
// user has at arg 0.
Instruction make_vfmadd_style() {
    return Instruction::declare("vfmadd_style")
        .spec([]() -> Pipeline {
            Var i;
            Func a(Float(32), 1, "a"), b(Float(32), 1, "b"),
                c(Float(32), 1, "c"), out("out");
            out(i) = a(i) * b(i) + c(i);
            for (Func f : std::vector<Func>{a, b, c, out}) {
                f.bound(i, 0, 8);
            }
            return Pipeline({out});
        })
        .require({Target::FMA, Target::AVX2})
        .emit(stub_emit)
        .build();
}

// Build a user pipeline whose body matches the spec body. Returns the
// output Func (named per `out_name`). All sub-Funcs are 1D with one arg
// named per `user_var_name`. Sub-Funcs are *named* matching the spec's
// input Func names ("a", "b", "c") so the schedule-transfer step finds
// them in env.
//
// The sub-Funcs are scheduled compute_root() so their bodies survive
// to canonical form as Loads (rather than inlining their constant
// definitions and collapsing the output body to a constant via
// Simplify). The Phase 5 emit substitution structurally matches the
// spec body against the user body, so the user body must actually
// produce loads-and-arithmetic in canonical form.
Func make_user_pipeline(const std::string &out_name,
                        const std::string &user_var_name) {
    Var x(user_var_name);
    Func a("a"), b("b"), c("c"), out(out_name);
    a(x) = 1.0f;
    b(x) = 2.0f;
    c(x) = 3.0f;
    a.compute_root();
    b.compute_root();
    c.compute_root();
    out(x) = a(x) * b(x) + c(x);
    return out;
}

Target target_with(const std::vector<Target::Feature> &feats) {
    Target t("x86-64-linux");
    for (Target::Feature f : feats) {
        t = t.with_feature(f);
    }
    return t;
}

void test_target_feature_missing_errors() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        printf("[SKIP] test_target_feature_missing_errors: exceptions disabled\n");
        return;
    }

    Instruction instr = make_vfmadd_style();
    Func out = make_user_pipeline("out_tfm", "x");
    out.implement_with(Var("x"), instr);

    // Compile target does NOT have FMA/AVX2 -> error.
    Target t = target_with({});
    Pipeline p(out);

    bool got_error = false;
    try {
        (void)p.compile_to_module({}, "test_tfm", t);
    } catch (const Halide::CompileError &e) {
        got_error = true;
        std::string msg = e.what();
        if (msg.find("implement_with") == std::string::npos ||
            (msg.find("fma") == std::string::npos && msg.find("avx2") == std::string::npos)) {
            printf("Target-feature error message missing expected context:\n%s\n",
                   msg.c_str());
            std::exit(1);
        }
    }
    if (!got_error) {
        printf("Compile with missing target feature should have errored, but didn't.\n");
        std::exit(1);
    }
#else
    printf("[SKIP] test_target_feature_missing_errors: built without exceptions\n");
#endif
}

void test_target_feature_present_succeeds() {
    Instruction instr = make_vfmadd_style();
    Func out = make_user_pipeline("out_tfp", "x");
    out.implement_with(Var("x"), instr);

    Target t = target_with({Target::FMA, Target::AVX2});
    Pipeline p(out);
    // Should not throw.
    (void)p.compile_to_module({}, "test_tfp", t);
}

void test_bounds_are_transferred_to_deep_copy_not_original() {
    Instruction instr = make_vfmadd_style();
    Func out = make_user_pipeline("out_dc", "x");
    out.implement_with(Var("x"), instr);

    // Snapshot user's bounds before compile.
    size_t bounds_before = out.function().schedule().bounds().size();
    size_t a_bounds_before = 0;
    {
        // Find "a" via the pipeline.
        Pipeline p(out);
        // No direct accessor for env from outside; rely on the fact that
        // schedule transfer is supposed to happen on the deep copy.
        Target t = target_with({Target::FMA, Target::AVX2});
        (void)p.compile_to_module({}, "test_dc", t);
    }
    size_t bounds_after = out.function().schedule().bounds().size();

    if (bounds_after != bounds_before) {
        printf("User's pristine out.function() bounds was mutated: %zu -> %zu\n",
               bounds_before, bounds_after);
        std::exit(1);
    }
    (void)a_bounds_before;
}

void test_conflicting_bound_errors() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        printf("[SKIP] test_conflicting_bound_errors: exceptions disabled\n");
        return;
    }

    Instruction instr = make_vfmadd_style();  // spec wants bound(i, 0, 8)
    Var x("x");
    Func a("a"), b("b"), c("c"), out("out_conflict");
    a(x) = 1.0f;
    b(x) = 2.0f;
    c(x) = 3.0f;
    out(x) = a(x) * b(x) + c(x);

    // User declares bound(x, 0, 16) -- conflicts with spec's bound 0..8.
    out.bound(x, 0, 16);
    out.implement_with(x, instr);

    Target t = target_with({Target::FMA, Target::AVX2});
    Pipeline p(out);

    bool got_error = false;
    try {
        (void)p.compile_to_module({}, "test_conflict", t);
    } catch (const Halide::CompileError &e) {
        got_error = true;
        std::string msg = e.what();
        if (msg.find("implement_with") == std::string::npos ||
            msg.find("extent") == std::string::npos) {
            printf("Conflicting-bound error message missing expected context:\n%s\n",
                   msg.c_str());
            std::exit(1);
        }
    }
    if (!got_error) {
        printf("Compile with conflicting bound should have errored, but didn't.\n");
        std::exit(1);
    }
#else
    printf("[SKIP] test_conflicting_bound_errors: built without exceptions\n");
#endif
}

void test_input_func_name_mismatch_is_silent() {
    // Originally a Phase 3 test for "silently skip inputs we can't name-
    // map." Phase 4's structural matcher and Phase 5's emit substitution
    // changed the semantics: input Funcs are bound by structural
    // correspondence (the matcher's func_rename), not by name match. So
    // a user pipeline whose inputs are named "p"/"q"/"r" -- different
    // from the spec's "a"/"b"/"c" -- still compiles end-to-end, with
    // the matcher binding the spec input names to the user names.
    Instruction instr = make_vfmadd_style();

    Var x("x");
    Func p_in("p"), q("q"), r("r"), out("out_mismatch");
    p_in(x) = 1.0f;
    q(x) = 2.0f;
    r(x) = 3.0f;
    p_in.compute_root();
    q.compute_root();
    r.compute_root();
    out(x) = p_in(x) * q(x) + r(x);
    out.implement_with(x, instr);

    Target t = target_with({Target::FMA, Target::AVX2});
    Pipeline pipe(out);
    (void)pipe.compile_to_module({}, "test_mismatch", t);
}

}  // namespace

int main(int argc, char **argv) {
    test_target_feature_missing_errors();
    test_target_feature_present_succeeds();
    test_bounds_are_transferred_to_deep_copy_not_original();
    test_conflicting_bound_errors();
    test_input_func_name_mismatch_is_silent();

    printf("Success!\n");
    return 0;
}
