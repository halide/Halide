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

// Match a lowered spec loop against itself. The matcher must report
// success; var_rename and func_rename should contain identity bindings
// (every spec name maps to itself, since the inputs are identical).
void test_match_identity_self() {
    Instruction instr = make_vfmadd_style_named();
    Pipeline spec = instr.spec();
    Target t = target_with({Target::FMA, Target::AVX2});
    std::string out_name = spec.outputs()[0].name();

    Internal::Stmt s = Internal::lower_spec_to_canonical_form(spec, t);
    Internal::Stmt loop =
        Internal::find_implement_with_loop(s, out_name, 0, "i");
    if (!loop.defined()) {
        fprintf(stderr, "test_match_identity_self: locator returned "
                        "undefined Stmt\n");
        exit(1);
    }
    Internal::MatchResult r = Internal::match_canonical_form(loop, loop);
    if (!r.success) {
        fprintf(stderr, "test_match_identity_self failed: %s\n",
                r.failure_reason.c_str());
        exit(1);
    }
    for (const auto &kv : r.var_rename) {
        if (kv.first != kv.second) {
            fprintf(stderr,
                    "test_match_identity_self: identity-match produced non-"
                    "identity var binding '%s' -> '%s'\n",
                    kv.first.c_str(), kv.second.c_str());
            exit(1);
        }
    }
    for (const auto &kv : r.func_rename) {
        if (kv.first != kv.second) {
            fprintf(stderr,
                    "test_match_identity_self: identity-match produced non-"
                    "identity func binding '%s' -> '%s'\n",
                    kv.first.c_str(), kv.second.c_str());
            exit(1);
        }
    }
}

// Construct two handmade Stmts that differ only in commutative
// reordering (x + y vs y + x). The matcher must report success and
// produce the alpha-rename bindings { x -> y, y -> x }.
void test_match_commutativity_directly() {
    using Halide::Internal::Evaluate;
    using Halide::Internal::MatchResult;
    using Halide::Internal::Stmt;
    using Halide::Internal::Variable;

    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    Stmt s1 = Evaluate::make(x + y);
    Stmt s2 = Evaluate::make(y + x);

    MatchResult r = Internal::match_canonical_form(s1, s2);
    if (!r.success) {
        fprintf(stderr,
                "test_match_commutativity_directly failed: %s\n",
                r.failure_reason.c_str());
        exit(1);
    }
    auto it_x = r.var_rename.find("x");
    auto it_y = r.var_rename.find("y");
    if (it_x == r.var_rename.end() || it_y == r.var_rename.end()) {
        fprintf(stderr,
                "test_match_commutativity_directly: missing bindings\n");
        exit(1);
    }
    if (it_x->second != "y" || it_y->second != "x") {
        fprintf(stderr,
                "test_match_commutativity_directly: expected x->y, y->x; "
                "got x->%s, y->%s\n",
                it_x->second.c_str(), it_y->second.c_str());
        exit(1);
    }
}

// Construct two handmade Stmts whose top-level Expr nodes have
// different opcodes (Add vs Mul). The matcher must report failure
// with a non-empty failure_reason.
void test_match_different_op_fails() {
    using Halide::Internal::Evaluate;
    using Halide::Internal::MatchResult;
    using Halide::Internal::Stmt;
    using Halide::Internal::Variable;

    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    Stmt s1 = Evaluate::make(x + y);
    Stmt s2 = Evaluate::make(x * y);

    MatchResult r = Internal::match_canonical_form(s1, s2);
    if (r.success) {
        fprintf(stderr,
                "test_match_different_op_fails: unexpected match success\n");
        exit(1);
    }
    if (r.failure_reason.empty()) {
        fprintf(stderr,
                "test_match_different_op_fails: empty failure_reason\n");
        exit(1);
    }
}

// Lower two specs of the same shape but with different Func name
// uniquification (two separate Instruction instances of the FMA
// spec). The For loop names and Func names differ; alpha-renaming
// must make the match succeed AND the For-loop var binding must be
// non-identity (proves the renaming machinery is actually exercised
// rather than the two Stmts being incidentally equal).
//
// Note: spec input Funcs auto-stub to 0.0f and are scheduled inline,
// so after Simplify the body collapses to Store(out, 0.0f). The only
// names that differ between the two lowerings are the output Func
// name and the loop-var name derived from it (e.g. "out$5.s0.i" vs
// "out$6.s0.i"). That is enough to validate alpha-renaming on the
// loop var; spec-input-Func wildcard semantics will be exercised by
// a follow-up commit that prevents stub inlining.
void test_match_alpha_rename_two_lowered_specs() {
    Pipeline spec1 = make_vfmadd_style_named().spec();
    Pipeline spec2 = make_vfmadd_style_named().spec();
    Target t = target_with({Target::FMA, Target::AVX2});
    std::string out1 = spec1.outputs()[0].name();
    std::string out2 = spec2.outputs()[0].name();
    Internal::Stmt s1 = Internal::lower_spec_to_canonical_form(spec1, t);
    Internal::Stmt s2 = Internal::lower_spec_to_canonical_form(spec2, t);
    Internal::Stmt l1 = Internal::find_implement_with_loop(s1, out1, 0, "i");
    Internal::Stmt l2 = Internal::find_implement_with_loop(s2, out2, 0, "i");
    if (!l1.defined() || !l2.defined()) {
        fprintf(stderr,
                "test_match_alpha_rename_two_lowered_specs: one of the "
                "locator calls returned undefined (out1=%s, out2=%s)\n",
                out1.c_str(), out2.c_str());
        exit(1);
    }
    if (out1 == out2) {
        fprintf(stderr,
                "test_match_alpha_rename_two_lowered_specs: precondition "
                "failed -- both spec outputs uniquified to the same name "
                "'%s'; this test relies on uniquification picking distinct "
                "names\n",
                out1.c_str());
        exit(1);
    }

    Internal::MatchResult r = Internal::match_canonical_form(l1, l2);
    if (!r.success) {
        fprintf(stderr,
                "test_match_alpha_rename_two_lowered_specs failed: %s\n",
                r.failure_reason.c_str());
        exit(1);
    }
    bool has_non_identity = false;
    for (const auto &kv : r.var_rename) {
        if (kv.first != kv.second) {
            has_non_identity = true;
            break;
        }
    }
    if (!has_non_identity) {
        fprintf(stderr,
                "test_match_alpha_rename_two_lowered_specs: matcher "
                "succeeded but produced no non-identity var bindings "
                "(out1=%s, out2=%s) -- alpha-renaming was not exercised\n",
                out1.c_str(), out2.c_str());
        exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    test_spec_lowers_to_nonempty_stmt();
    test_use_site_pipeline_still_compiles();
    test_find_implement_with_loop_returns_for_node();
    test_find_implement_with_loop_returns_undefined_when_missing();
    test_match_identity_self();
    test_match_commutativity_directly();
    test_match_different_op_fails();
    test_match_alpha_rename_two_lowered_specs();

    printf("Success!\n");
    return 0;
}
