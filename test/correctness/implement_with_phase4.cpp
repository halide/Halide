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
// rather than the two Stmts being incidentally equal). The
// dedicated test for spec-input-Func wildcard binding via
// func_rename is test_match_spec_input_funcs_bind_as_wildcards.
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

// Spec-input Func wildcard binding: when spec input Funcs are auto-
// stubbed and scheduled compute_root() (which Phase 2's spec-thunk
// auto-stub now does by default), their bodies survive the canonical-
// form prefix as Realize / Allocate / Load chains rather than being
// inlined and Simplified to constants. The matcher's func_rename map
// then binds the spec input names to whichever names the user-side IR
// uses at the corresponding Load positions.
//
// Without this, the spec input Funcs degenerate to 0.0f and the inner
// loop body collapses to Store(out, 0.0f) -- there are no Loads from
// 'a' / 'b' / 'c' for the matcher's func_rename to bind. This test
// proves the wildcard behavior is observable end to end.
class CollectAllocateNames : public Internal::IRVisitor {
public:
    std::vector<std::string> names;
    using Internal::IRVisitor::visit;
    void visit(const Internal::Allocate *op) override {
        names.push_back(op->name);
        Internal::IRVisitor::visit(op);
    }
};

void test_match_spec_input_funcs_bind_as_wildcards() {
    Pipeline spec1 = make_vfmadd_style_named().spec();
    Pipeline spec2 = make_vfmadd_style_named().spec();
    Target t = target_with({Target::FMA, Target::AVX2});
    std::string out1 = spec1.outputs()[0].name();
    std::string out2 = spec2.outputs()[0].name();
    Internal::Stmt s1 = Internal::lower_spec_to_canonical_form(spec1, t);
    Internal::Stmt s2 = Internal::lower_spec_to_canonical_form(spec2, t);

    CollectAllocateNames cn1, cn2;
    s1.accept(&cn1);
    s2.accept(&cn2);
    if (cn1.names.size() != 3 || cn2.names.size() != 3) {
        fprintf(stderr,
                "test_match_spec_input_funcs_bind_as_wildcards: expected "
                "3 spec input allocations per side, got %zu vs %zu\n",
                cn1.names.size(), cn2.names.size());
        exit(1);
    }

    Internal::Stmt l1 = Internal::find_implement_with_loop(s1, out1, 0, "i");
    Internal::Stmt l2 = Internal::find_implement_with_loop(s2, out2, 0, "i");
    if (!l1.defined() || !l2.defined()) {
        fprintf(stderr,
                "test_match_spec_input_funcs_bind_as_wildcards: locator "
                "returned undefined Stmt (out1=%s, out2=%s)\n",
                out1.c_str(), out2.c_str());
        exit(1);
    }
    Internal::MatchResult r = Internal::match_canonical_form(l1, l2);
    if (!r.success) {
        fprintf(stderr,
                "test_match_spec_input_funcs_bind_as_wildcards: match "
                "failed: %s\n",
                r.failure_reason.c_str());
        exit(1);
    }

    // Every spec input Allocate name from side 1 must appear in
    // func_rename and bind to one of side 2's input Allocate names. We
    // do not insist on a particular pairing because the matcher is free
    // to choose commutative orderings (e.g. a <-> b can be swapped
    // under Mul without affecting the match).
    for (const std::string &n : cn1.names) {
        auto it = r.func_rename.find(n);
        if (it == r.func_rename.end()) {
            fprintf(stderr,
                    "test_match_spec_input_funcs_bind_as_wildcards: "
                    "missing func_rename entry for spec input '%s'\n",
                    n.c_str());
            exit(1);
        }
        bool found = false;
        for (const std::string &n2 : cn2.names) {
            if (it->second == n2) {
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr,
                    "test_match_spec_input_funcs_bind_as_wildcards: "
                    "spec input '%s' bound to '%s' which is not a "
                    "side-2 spec input\n",
                    n.c_str(), it->second.c_str());
            exit(1);
        }
    }
}

// Simplify-equivalence fallback: structurally distinct but
// algebraically equivalent integer Exprs match once their free
// Variables are alpha-bound. Spec body uses (i + 4) - 2; user body
// uses j + 2; the For loop's bind i <-> j enables the matcher's
// substitute + simplify rule to declare equivalence.
void test_match_simplify_equivalent_integer_indices() {
    using Halide::Internal::Evaluate;
    using Halide::Internal::For;
    using Halide::Internal::ForType;
    using Halide::Internal::MatchResult;
    using Halide::Internal::Stmt;
    using Halide::Internal::Variable;

    Expr i = Variable::make(Int(32), "i");
    Expr j = Variable::make(Int(32), "j");

    Stmt body_spec = Evaluate::make((i + 4) - 2);
    Stmt body_user = Evaluate::make(j + 2);

    Stmt s1 = For::make("i", 0, 8, ForType::Serial, Partition::Auto,
                        DeviceAPI::None, body_spec);
    Stmt s2 = For::make("j", 0, 8, ForType::Serial, Partition::Auto,
                        DeviceAPI::None, body_user);

    MatchResult r = Internal::match_canonical_form(s1, s2);
    if (!r.success) {
        fprintf(stderr,
                "test_match_simplify_equivalent_integer_indices: "
                "match failed: %s\n",
                r.failure_reason.c_str());
        exit(1);
    }
    auto it = r.var_rename.find("i");
    if (it == r.var_rename.end() || it->second != "j") {
        fprintf(stderr,
                "test_match_simplify_equivalent_integer_indices: "
                "expected For binding i -> j; got '%s'\n",
                (it == r.var_rename.end() ? "(missing)" : it->second.c_str()));
        exit(1);
    }
}

// Negative case: integer Exprs that aren't algebraically equivalent
// must still fail to match. Spec body is i + 3; user body is j + 2;
// after binding i -> j the difference simplifies to 1, not zero.
void test_match_simplify_unequal_integers_still_fail() {
    using Halide::Internal::Evaluate;
    using Halide::Internal::For;
    using Halide::Internal::ForType;
    using Halide::Internal::MatchResult;
    using Halide::Internal::Stmt;
    using Halide::Internal::Variable;

    Expr i = Variable::make(Int(32), "i");
    Expr j = Variable::make(Int(32), "j");

    Stmt body_spec = Evaluate::make(i + 3);
    Stmt body_user = Evaluate::make(j + 2);

    Stmt s1 = For::make("i", 0, 8, ForType::Serial, Partition::Auto,
                        DeviceAPI::None, body_spec);
    Stmt s2 = For::make("j", 0, 8, ForType::Serial, Partition::Auto,
                        DeviceAPI::None, body_user);

    MatchResult r = Internal::match_canonical_form(s1, s2);
    if (r.success) {
        fprintf(stderr,
                "test_match_simplify_unequal_integers_still_fail: "
                "unexpected success on (i+3) vs (j+2)\n");
        exit(1);
    }
}

// ---------------------------------------------------------------------------
// Case study: vfmadd231ps_256 (the canonical §3.3 example)
// ---------------------------------------------------------------------------
// End-to-end matcher validation against a real user pipeline. Lowers
// both the spec (using lower_spec_to_canonical_form) and a realistic
// Halide user pipeline (using lower_pipeline_to_canonical_form), then
// runs the structural matcher on the inner For nodes. Asserts that
//   - the match succeeds,
//   - var_rename maps the spec loop var to the user loop var (or to a
//     name uniquification thereof), and
//   - func_rename binds every spec input Func name (a, b, c) and the
//     spec output name to a corresponding user-side buffer / output
//     name.
//
// The user pipeline uses ImageParam inputs with min pinned to 0 so
// that the generated index is `x` rather than `x - ua.min.0`. Without
// the pinned mins, the user-side Loads carry a parameter-dependent
// offset that the matcher's Simplify-equivalence fallback cannot
// prove constant. That non-zero-min handling is naturally Phase 7
// (affine match parameters) territory and out of scope for v1.
void test_case_study_vfmadd231ps_256() {
    Instruction instr = make_vfmadd_style_named();
    Pipeline spec = instr.spec();
    Target t = target_with({Target::FMA, Target::AVX2});
    std::string spec_out = spec.outputs()[0].name();

    ImageParam a_in(Float(32), 1, "ua");
    ImageParam b_in(Float(32), 1, "ub");
    ImageParam c_in(Float(32), 1, "uc");
    a_in.dim(0).set_bounds(0, 8);
    b_in.dim(0).set_bounds(0, 8);
    c_in.dim(0).set_bounds(0, 8);
    Func out("user_out_vfmadd");
    Var x("x");
    out(x) = a_in(x) * b_in(x) + c_in(x);
    out.bound(x, 0, 8);
    Pipeline user(out);

    Internal::Stmt spec_s = Internal::lower_spec_to_canonical_form(spec, t);
    Internal::Stmt user_s = Internal::lower_pipeline_to_canonical_form(user, t);

    Internal::Stmt spec_loop =
        Internal::find_implement_with_loop(spec_s, spec_out, 0, "i");
    Internal::Stmt user_loop =
        Internal::find_implement_with_loop(user_s, "user_out_vfmadd", 0, "x");
    if (!spec_loop.defined() || !user_loop.defined()) {
        fprintf(stderr,
                "test_case_study_vfmadd231ps_256: locator missed "
                "(spec_defined=%d user_defined=%d)\n",
                (int)spec_loop.defined(), (int)user_loop.defined());
        exit(1);
    }

    Internal::MatchResult r =
        Internal::match_canonical_form(spec_loop, user_loop);
    if (!r.success) {
        fprintf(stderr,
                "test_case_study_vfmadd231ps_256: match failed: %s\n",
                r.failure_reason.c_str());
        exit(1);
    }

    // Loop var: spec For is "<spec_out>.s0.i"; user For is
    // "user_out_vfmadd.s0.x". The matcher binds the full For-loop
    // names in var_rename, so we look up the spec For name (which is
    // also the same string as spec_loop's For::name).
    const Internal::For *spec_for = spec_loop.as<Internal::For>();
    const Internal::For *user_for = user_loop.as<Internal::For>();
    internal_assert(spec_for && user_for) << "locator returned non-For Stmt";
    auto it = r.var_rename.find(spec_for->name);
    if (it == r.var_rename.end() || it->second != user_for->name) {
        fprintf(stderr,
                "test_case_study_vfmadd231ps_256: expected For-name binding "
                "'%s' -> '%s'; got '%s'\n",
                spec_for->name.c_str(), user_for->name.c_str(),
                (it == r.var_rename.end() ? "(missing)" : it->second.c_str()));
        exit(1);
    }

    // Func bindings: spec primary 'out' binds to user 'user_out_vfmadd';
    // spec inputs 'a', 'b', 'c' bind to user ImageParam names
    // 'ua', 'ub', 'uc'. Names are uniquified per process, so look up
    // by inspecting the spec-side Allocate names.
    CollectAllocateNames spec_allocs;
    spec_s.accept(&spec_allocs);
    for (const std::string &n : spec_allocs.names) {
        auto bit = r.func_rename.find(n);
        if (bit == r.func_rename.end()) {
            fprintf(stderr,
                    "test_case_study_vfmadd231ps_256: spec Allocate '%s' "
                    "is unbound in func_rename\n",
                    n.c_str());
            exit(1);
        }
        // Bound name should start with "u" (ua / ub / uc).
        if (bit->second.empty() || bit->second[0] != 'u') {
            fprintf(stderr,
                    "test_case_study_vfmadd231ps_256: spec input '%s' "
                    "bound to '%s', which is not a user ImageParam "
                    "(ua/ub/uc)\n",
                    n.c_str(), bit->second.c_str());
            exit(1);
        }
    }

    // Spec output should bind to the user output.
    auto out_it = r.func_rename.find(spec_out);
    if (out_it == r.func_rename.end() || out_it->second != "user_out_vfmadd") {
        fprintf(stderr,
                "test_case_study_vfmadd231ps_256: spec output '%s' "
                "expected to bind to 'user_out_vfmadd'; got '%s'\n",
                spec_out.c_str(),
                (out_it == r.func_rename.end() ? "(missing)" : out_it->second.c_str()));
        exit(1);
    }
}

// ---------------------------------------------------------------------------
// Case study: SDOT-style 4x4 GEMV (DESIGN.md §3.4)
// ---------------------------------------------------------------------------
// Single-output ARM dot-product GEMV. The spec is a two-stage Func:
//   out(i) = c(i);
//   out(i) += int32(A(i, k)) * int32(b(k));
// matched at the update stage's i-loop. The user pipeline mirrors the
// shape with ImageParam inputs. This exercises:
//   - 2D Loads (A) with both bound and stride pinned at the ImageParam,
//   - the matcher walking through an inner reduction-domain For loop,
//   - Cast nodes from int8 -> int32 (the actual SDOT widening),
//   - the matcher's two-stage out (init + update) — find_implement_with_loop
//     locates the update-stage's For specifically.
//
// Joint multi-output matching (the "four broadcasting SDOTs" property in
// the design) is task #9 and out of scope here; this case study is the
// single-output reduction variant the §3.4 example reduces to once the
// emit-side multi-instruction structure is factored out.
Instruction make_sdot_style_named() {
    return Instruction::declare("sdot_gemv_4x4_named")
        .spec([]() -> Pipeline {
            Var i("i"), j("j");
            RDom k(0, 4, "k");
            Func A(Int(8), 2, "A"), b(Int(8), 1, "b"),
                c(Int(32), 1, "c"), out(Int(32), 1, "out");
            // Force the c(i) FuncRef -> Expr conversion explicitly: the
            // FuncRef-to-FuncRef operator= overload would otherwise route
            // through Tuple(FuncRef), which asserts on undefined Funcs
            // before the spec-thunk auto-stub gets a chance to fire. The
            // auto-stub only triggers from FuncRef::operator Expr().
            Expr c_at_i = c(i);
            out(i) = c_at_i;
            out(i) += cast<int32_t>(A(i, k)) * cast<int32_t>(b(k));
            // Don't bound the auto-stubbed inputs. Their pure args take
            // their names from the call site (A: "i","k$0"; b: "k$0";
            // c: "i"), so `bound(_1, 0, 4)` or `bound(i, 0, 4)` on b
            // doesn't resolve. Bound inference picks up the inputs'
            // required regions from the consumer `out`, which has its
            // first dim bounded explicitly and its second access driven
            // by the constant-extent RDom k(0, 4).
            out.bound(i, 0, 4);
            return Pipeline({out});
        })
        .require({})
        .emit(stub_emit)
        .build();
}

void test_case_study_sdot_gemv_4x4() {
    Instruction instr = make_sdot_style_named();
    Pipeline spec = instr.spec();
    Target t = target_with({});
    std::string spec_out = spec.outputs()[0].name();

    ImageParam A_in(Int(8), 2, "uA");
    ImageParam b_in(Int(8), 1, "ub");
    ImageParam c_in(Int(32), 1, "uc");
    A_in.dim(0).set_bounds(0, 4).set_stride(1);
    A_in.dim(1).set_bounds(0, 4).set_stride(4);
    b_in.dim(0).set_bounds(0, 4);
    c_in.dim(0).set_bounds(0, 4);
    Func uout(Int(32), 1, "user_out_sdot");
    Var x("x");
    RDom uk(0, 4, "uk");
    uout(x) = c_in(x);
    uout(x) += cast<int32_t>(A_in(x, uk)) * cast<int32_t>(b_in(uk));
    uout.bound(x, 0, 4);
    Pipeline user(uout);

    Internal::Stmt spec_s = Internal::lower_spec_to_canonical_form(spec, t);
    Internal::Stmt user_s = Internal::lower_pipeline_to_canonical_form(user, t);

    Internal::Stmt spec_loop =
        Internal::find_implement_with_loop(spec_s, spec_out, 1, "i");
    Internal::Stmt user_loop =
        Internal::find_implement_with_loop(user_s, "user_out_sdot", 1, "x");
    if (!spec_loop.defined() || !user_loop.defined()) {
        fprintf(stderr,
                "test_case_study_sdot_gemv_4x4: locator missed "
                "(spec_defined=%d user_defined=%d). spec IR:\n%s\nuser IR:\n%s\n",
                (int)spec_loop.defined(), (int)user_loop.defined(),
                ([&] { std::ostringstream os; os << spec_s; return os.str(); })().c_str(),
                ([&] { std::ostringstream os; os << user_s; return os.str(); })().c_str());
        exit(1);
    }

    Internal::MatchResult r =
        Internal::match_canonical_form(spec_loop, user_loop);
    if (!r.success) {
        fprintf(stderr,
                "test_case_study_sdot_gemv_4x4: match failed: %s\n"
                "spec loop:\n%s\nuser loop:\n%s\n",
                r.failure_reason.c_str(),
                ([&] { std::ostringstream os; os << spec_loop; return os.str(); })().c_str(),
                ([&] { std::ostringstream os; os << user_loop; return os.str(); })().c_str());
        exit(1);
    }

    const Internal::For *spec_for = spec_loop.as<Internal::For>();
    const Internal::For *user_for = user_loop.as<Internal::For>();
    internal_assert(spec_for && user_for) << "locator returned non-For Stmt";
    auto it = r.var_rename.find(spec_for->name);
    if (it == r.var_rename.end() || it->second != user_for->name) {
        fprintf(stderr,
                "test_case_study_sdot_gemv_4x4: expected For-name binding "
                "'%s' -> '%s'; got '%s'\n",
                spec_for->name.c_str(), user_for->name.c_str(),
                (it == r.var_rename.end() ? "(missing)" : it->second.c_str()));
        exit(1);
    }

    // The matched region (update-stage s1 i-loop) references only the
    // reduction-side inputs A and b plus the in-place output `out`. The
    // init-stage input `c` lives in s0 and is therefore not visible
    // here; its binding belongs to a (currently out-of-scope) match
    // against the s0 For. We verify the bindings we *do* expect:
    //   - spec output 'out' binds to 'user_out_sdot',
    //   - exactly two spec input Allocates bind to user ImageParams
    //     whose names start with 'u' (uA and ub).
    auto out_it = r.func_rename.find(spec_out);
    if (out_it == r.func_rename.end() || out_it->second != "user_out_sdot") {
        fprintf(stderr,
                "test_case_study_sdot_gemv_4x4: spec output '%s' "
                "expected to bind to 'user_out_sdot'; got '%s'\n",
                spec_out.c_str(),
                (out_it == r.func_rename.end() ? "(missing)" : out_it->second.c_str()));
        exit(1);
    }

    CollectAllocateNames spec_allocs;
    spec_s.accept(&spec_allocs);
    int n_inputs_bound = 0;
    for (const std::string &n : spec_allocs.names) {
        if (n == spec_out) continue;
        auto bit = r.func_rename.find(n);
        if (bit == r.func_rename.end()) {
            continue;  // input not referenced in the matched region (e.g. c)
        }
        if (bit->second.empty() || bit->second[0] != 'u') {
            fprintf(stderr,
                    "test_case_study_sdot_gemv_4x4: spec input '%s' "
                    "bound to '%s', which is not a user ImageParam "
                    "(uA/ub/uc)\n",
                    n.c_str(), bit->second.c_str());
            exit(1);
        }
        ++n_inputs_bound;
    }
    if (n_inputs_bound != 2) {
        fprintf(stderr,
                "test_case_study_sdot_gemv_4x4: expected 2 spec inputs "
                "(A and b) bound to user ImageParams in the update-stage "
                "region; got %d\n",
                n_inputs_bound);
        exit(1);
    }
}

// ---------------------------------------------------------------------------
// Case study: HVX widening MAC via find_intrinsics
// ---------------------------------------------------------------------------
// Validates that intrinsic lifting (find_intrinsics) inside the
// canonical-form prefix produces match-friendly IR on both sides. The
// pattern is a classic HVX MAC: out(i) += widening_mul(a(i), b(i)),
// authored as cast<int32>(a) * cast<int32>(b) on both sides;
// find_intrinsics lifts it to a `widening_mul` Call. If lifting were
// inconsistent (or skipped on one side), the matcher would see a Mul
// of Casts on one side and a widening_mul Call on the other, failing
// with a node-type mismatch. The test asserts they match.
//
// Target: hexagon-32-noos-hvx_v66 (the bundled halide-llvm includes
// Hexagon). The match property is generic to any target that runs
// find_intrinsics in canonical form (i.e. all of them), but using a
// real HVX target exercises the Hexagon-specific gating that motivates
// this case study.
Instruction make_hvx_mac_style_named() {
    return Instruction::declare("hvx_mac_style_named")
        .spec([]() -> Pipeline {
            Var i("i"), io("io"), ii("ii");
            Func a(Int(8), 1, "a"), b(Int(8), 1, "b"),
                out(Int(32), 1, "out");
            Expr a_at_i = a(i);
            Expr b_at_i = b(i);
            out(i) = cast<int32_t>(0);
            out(i) += cast<int32_t>(a_at_i) * cast<int32_t>(b_at_i);
            out.bound(i, 0, 128);
            // Vectorization is required for find_intrinsics to lift
            // widening_mul (its guard `find_intrinsics_for_type` is
            // vector-only). HVX has 64 int32 lanes at v66; split i into
            // outer io / inner ii(64) and vectorize the inner, giving
            // the post-split outer For a deterministic name
            // ("out.s1.io") that find_implement_with_loop can locate.
            out.update(0).split(i, io, ii, 64).vectorize(ii);
            return Pipeline({out});
        })
        .require({})
        .emit(stub_emit)
        .build();
}

class CountWideningMul : public Internal::IRVisitor {
public:
    int count = 0;
    using Internal::IRVisitor::visit;
    void visit(const Internal::Call *op) override {
        if (op->is_intrinsic(Internal::Call::widening_mul)) {
            ++count;
        }
        Internal::IRVisitor::visit(op);
    }
};

void test_case_study_hvx_mac_widening() {
    Instruction instr = make_hvx_mac_style_named();
    Pipeline spec = instr.spec();
    Target t("hexagon-32-noos-hvx_v66");
    std::string spec_out = spec.outputs()[0].name();

    ImageParam a_in(Int(8), 1, "ua");
    ImageParam b_in(Int(8), 1, "ub");
    a_in.dim(0).set_bounds(0, 128);
    b_in.dim(0).set_bounds(0, 128);
    Func uout(Int(32), 1, "user_out_hvx_mac");
    Var x("x"), xo("xo"), xi("xi");
    uout(x) = cast<int32_t>(0);
    uout(x) += cast<int32_t>(a_in(x)) * cast<int32_t>(b_in(x));
    uout.bound(x, 0, 128);
    uout.update(0).split(x, xo, xi, 64).vectorize(xi);
    Pipeline user(uout);

    Internal::Stmt spec_s = Internal::lower_spec_to_canonical_form(spec, t);
    Internal::Stmt user_s = Internal::lower_pipeline_to_canonical_form(user, t);

    // Sanity-check that find_intrinsics actually lifted widening_mul on
    // both sides. If it didn't, this test would not be exercising the
    // canonical-form intrinsic-lifting property at all.
    CountWideningMul spec_count, user_count;
    spec_s.accept(&spec_count);
    user_s.accept(&user_count);
    if (spec_count.count == 0 || user_count.count == 0) {
        fprintf(stderr,
                "test_case_study_hvx_mac_widening: find_intrinsics did "
                "not lift widening_mul (spec=%d, user=%d). Pattern was "
                "probably constant-folded away or the canonical-form "
                "prefix is no longer running find_intrinsics. spec IR:\n"
                "%s\nuser IR:\n%s\n",
                spec_count.count, user_count.count,
                ([&] { std::ostringstream os; os << spec_s; return os.str(); })().c_str(),
                ([&] { std::ostringstream os; os << user_s; return os.str(); })().c_str());
        exit(1);
    }

    // The post-split outer loop is named "<func>.s1.<orig>.<outer>" —
    // Halide's split() concatenates the original var name and the new
    // outer name. With original var "i" and outer "io", the spec For is
    // "<out>.s1.i.io"; the user For is "user_out_hvx_mac.s1.x.xo".
    Internal::Stmt spec_loop =
        Internal::find_implement_with_loop(spec_s, spec_out, 1, "i.io");
    Internal::Stmt user_loop =
        Internal::find_implement_with_loop(user_s, "user_out_hvx_mac", 1, "x.xo");
    if (!spec_loop.defined() || !user_loop.defined()) {
        fprintf(stderr,
                "test_case_study_hvx_mac_widening: locator missed "
                "(spec_defined=%d user_defined=%d). spec IR:\n%s\nuser IR:\n%s\n",
                (int)spec_loop.defined(), (int)user_loop.defined(),
                ([&] { std::ostringstream os; os << spec_s; return os.str(); })().c_str(),
                ([&] { std::ostringstream os; os << user_s; return os.str(); })().c_str());
        exit(1);
    }

    Internal::MatchResult r =
        Internal::match_canonical_form(spec_loop, user_loop);
    if (!r.success) {
        fprintf(stderr,
                "test_case_study_hvx_mac_widening: match failed: %s\n"
                "spec loop:\n%s\nuser loop:\n%s\n",
                r.failure_reason.c_str(),
                ([&] { std::ostringstream os; os << spec_loop; return os.str(); })().c_str(),
                ([&] { std::ostringstream os; os << user_loop; return os.str(); })().c_str());
        exit(1);
    }

    // The matched region should reference the widening_mul intrinsic
    // (proves it's the lifted form being matched, not the un-lifted
    // Mul of Casts that find_intrinsics started from).
    CountWideningMul loop_count;
    spec_loop.accept(&loop_count);
    if (loop_count.count == 0) {
        fprintf(stderr,
                "test_case_study_hvx_mac_widening: matched spec loop "
                "contains no widening_mul intrinsic; the test region "
                "was not the lifted form.\n");
        exit(1);
    }

    // Spec output binds to the user output; spec input a, b bind to
    // the user ImageParams (uniquification permitting).
    auto out_it = r.func_rename.find(spec_out);
    if (out_it == r.func_rename.end() || out_it->second != "user_out_hvx_mac") {
        fprintf(stderr,
                "test_case_study_hvx_mac_widening: spec output '%s' "
                "expected to bind to 'user_out_hvx_mac'; got '%s'\n",
                spec_out.c_str(),
                (out_it == r.func_rename.end() ? "(missing)" : out_it->second.c_str()));
        exit(1);
    }

    CollectAllocateNames spec_allocs;
    spec_s.accept(&spec_allocs);
    int n_inputs_bound = 0;
    for (const std::string &n : spec_allocs.names) {
        if (n == spec_out) continue;
        auto bit = r.func_rename.find(n);
        if (bit == r.func_rename.end()) continue;
        if (bit->second.empty() || bit->second[0] != 'u') {
            fprintf(stderr,
                    "test_case_study_hvx_mac_widening: spec input '%s' "
                    "bound to '%s', which is not a user ImageParam\n",
                    n.c_str(), bit->second.c_str());
            exit(1);
        }
        ++n_inputs_bound;
    }
    if (n_inputs_bound != 2) {
        fprintf(stderr,
                "test_case_study_hvx_mac_widening: expected 2 spec "
                "inputs (a, b) bound; got %d\n",
                n_inputs_bound);
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
    test_match_spec_input_funcs_bind_as_wildcards();
    test_match_simplify_equivalent_integer_indices();
    test_match_simplify_unequal_integers_still_fail();
    test_case_study_vfmadd231ps_256();
    test_case_study_sdot_gemv_4x4();
    test_case_study_hvx_mac_widening();

    printf("Success!\n");
    return 0;
}
