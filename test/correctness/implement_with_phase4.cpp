// Phase 4 test for the `implement_with` feature. See
// docs/implement_with/DESIGN.md §4.4 and §8.2 Phase 4, plus
// docs/implement_with/IMPLEMENTATION_STATUS.md.
//
// Exercises the matcher's spec-lowering entry point and (in subsequent
// sub-tests as Phase 4 lands) the structural matcher itself.

#include "Halide.h"

#include "ImplementWithMatcher.h"

#include <cstdio>
#include <sstream>
#include <string>

using namespace Halide;

namespace {

Internal::Stmt stub_emit(const MatchContext &) {
    return Internal::Evaluate::make(Expr(0));
}

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

// Same as make_vfmadd_style but uses an explicitly named Var("i") so
// that the lowered For node has a predictable stage-qualified name
// ("out.s0.i") for region-locator tests.
Instruction make_vfmadd_style_named() {
    return Instruction::declare("vfmadd_style_named")
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
        .require({Target::FMA, Target::AVX2})
        .emit(stub_emit)
        .build();
}

Target target_with(const std::vector<Target::Feature> &feats) {
    Target t("x86-64-linux");
    for (Target::Feature f : feats) {
        t = t.with_feature(f);
    }
    return t;
}

// Smoke test: lower a spec pipeline through the matcher's spec-lowering
// entry point and verify it produces a non-trivial Stmt containing the
// output Func's name. This validates that the spec-pattern Func's
// auto-stubbed input bodies do not prevent lowering through the
// canonical-form prefix.
void test_spec_lowers_to_nonempty_stmt() {
    Instruction instr = make_vfmadd_style();
    Pipeline spec = instr.spec();
    Target t = target_with({Target::FMA, Target::AVX2});

    Internal::Stmt s = Internal::lower_spec_to_canonical_form(spec, t);
    if (!s.defined()) {
        fprintf(stderr,
                "test_spec_lowers_to_nonempty_stmt: returned Stmt is "
                "undefined\n");
        exit(1);
    }

    // The lowered spec should mention the output Func's name somewhere
    // (in an allocation, a Produce node, or a store). Print and grep is
    // the cheap way to confirm we got real IR rather than an empty
    // skeleton.
    std::ostringstream os;
    os << s;
    std::string ir = os.str();
    if (ir.find("out") == std::string::npos) {
        fprintf(stderr,
                "test_spec_lowers_to_nonempty_stmt: lowered Stmt does "
                "not mention output Func 'out':\n%s\n",
                ir.c_str());
        exit(1);
    }
}

// The use-site lowering (via the existing apply_implement_with_directives
// pass) still works alongside spec lowering. This is a regression check
// that the canonical-form entry-point split didn't break the Phase 3
// pipeline.
void test_use_site_pipeline_still_compiles() {
    Instruction instr = make_vfmadd_style();
    Var x;
    Func a("a"), b("b"), c("c"), out("out_use_site");
    a(x) = 1.0f;
    b(x) = 2.0f;
    c(x) = 3.0f;
    out(x) = a(x) * b(x) + c(x);
    out.implement_with(x, instr);

    Target t = target_with({Target::FMA, Target::AVX2});
    Pipeline pipe(out);
    (void)pipe.compile_to_module({}, "test_use_site", t);
}

// Locate the For node corresponding to the directive's loop level in
// the spec's canonical-form IR. The spec's output Func name is
// discovered at runtime because Halide uniquifies Func names process-
// wide (the third "out" Func in a given process becomes "out$2"). The
// Var is the explicitly named "i" from make_vfmadd_style_named.
void test_find_implement_with_loop_returns_for_node() {
    Instruction instr = make_vfmadd_style_named();
    Pipeline spec = instr.spec();
    Target t = target_with({Target::FMA, Target::AVX2});
    std::string out_name = spec.outputs()[0].name();

    Internal::Stmt s = Internal::lower_spec_to_canonical_form(spec, t);
    Internal::Stmt loop =
        Internal::find_implement_with_loop(s, out_name, 0, "i");
    if (!loop.defined()) {
        fprintf(stderr,
                "test_find_implement_with_loop_returns_for_node: locator "
                "returned undefined Stmt for %s.s0.i; lowered IR was:\n%s\n",
                out_name.c_str(),
                ([&] {
                    std::ostringstream os;
                    os << s;
                    return os.str();
                })().c_str());
        exit(1);
    }
    const Internal::For *f = loop.as<Internal::For>();
    std::string expected = out_name + ".s0.i";
    if (!f || f->name != expected) {
        fprintf(stderr,
                "test_find_implement_with_loop_returns_for_node: returned "
                "Stmt is not the expected For '%s'\n",
                expected.c_str());
        exit(1);
    }
}

void test_find_implement_with_loop_returns_undefined_when_missing() {
    Instruction instr = make_vfmadd_style_named();
    Pipeline spec = instr.spec();
    Target t = target_with({Target::FMA, Target::AVX2});
    std::string out_name = spec.outputs()[0].name();

    Internal::Stmt s = Internal::lower_spec_to_canonical_form(spec, t);
    Internal::Stmt loop =
        Internal::find_implement_with_loop(s, out_name, 0, "no_such_var");
    if (loop.defined()) {
        fprintf(stderr,
                "test_find_implement_with_loop_returns_undefined_when_missing: "
                "locator unexpectedly found a For for a name that should not "
                "exist\n");
        exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    test_spec_lowers_to_nonempty_stmt();
    test_use_site_pipeline_still_compiles();
    test_find_implement_with_loop_returns_for_node();
    test_find_implement_with_loop_returns_undefined_when_missing();

    printf("Success!\n");
    return 0;
}
