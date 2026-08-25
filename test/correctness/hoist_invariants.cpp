#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

#include <cmath>
#include <map>

namespace {

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

// hoist_invariants() lifts a loop-invariant factor out of a sum reduction:
//   sum_k(scale(i,j) * inner(i,j,k)) = scale(i,j) * sum_k(inner(i,j,k))
// It does not change any types: the returned intermediate accumulates the
// factor-free body at its natural type. (Retyping the accumulation for a
// dot-product-friendly integer type is the job of change_type().)
int hoist_invariants_test() {
    ImageParam A{Int(8), 2, "A"};
    ImageParam B{Int(8), 2, "B"};
    ImageParam As{Float(16), 1, "As"};
    ImageParam Bs{Float(16), 1, "Bs"};

    Var i{"i"}, j{"j"};
    RDom k({{0, A.dim(1).extent() / 4 * 4}}, "k");
    RVar ko{"ko"}, ki{"ki"};

    Func C{"C"};
    C(i, j) += widening_mul(As(i), Bs(j)) * cast(Int(32), widening_mul(A(i, k), B(j, k)));
    C.bound(i, 0, A.dim(0).extent());
    C.bound(j, 0, B.dim(0).extent());
    C.update().split(k, ko, ki, 4);

    // widening_mul(As(i), Bs(j)) is invariant in k, so hoisting moves it out of
    // the reduction: the intermediate accumulates only the inner product and the
    // scale is applied once during write-back. The body's type (Float(32)) is
    // unchanged.
    Func C_intm = C.update().hoist_invariants();

    internal_assert(C_intm.types()[0] == Float(32))
        << "hoist_invariants: expected C_intm to keep its natural Float(32) type, got "
        << C_intm.types()[0] << "\n";

    // Numerical correctness: result must match a reference that applies the full
    // non-hoisted reduction.
    const int M = 8, N = 8, K = 16;
    Buffer<int8_t> a_buf(M, K), b_buf(N, K);
    Buffer<float16_t> as_buf(M), bs_buf(N);
    for (int m = 0; m < M; m++) {
        for (int n_k = 0; n_k < K; n_k++) {
            a_buf(m, n_k) = (int8_t)((m + n_k) % 7 - 3);
        }
        as_buf(m) = float16_t((float)(m + 1) * 0.5f);
    }
    for (int n = 0; n < N; n++) {
        for (int n_k = 0; n_k < K; n_k++) {
            b_buf(n, n_k) = (int8_t)((n + n_k + 1) % 5 - 2);
        }
        bs_buf(n) = float16_t((float)(n + 1) * 0.25f);
    }
    A.set(a_buf);
    B.set(b_buf);
    As.set(as_buf);
    Bs.set(bs_buf);

    Buffer<float> result = C.realize({M, N});

    // Reference: plain reduction without hoisting.
    Buffer<float> ref(M, N);
    ref.fill(0.f);
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            for (int kk = 0; kk < K; kk++) {
                ref(m, n) += (float)as_buf(m) * (float)bs_buf(n) *
                             (float)((int32_t)(int16_t)((int16_t)a_buf(m, kk) * (int16_t)b_buf(n, kk)));
            }
        }
    }

    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            internal_assert(std::abs(result(m, n) - ref(m, n)) < 1e-6f)
                << "hoist_invariants mismatch at (" << m << ", " << n << "): "
                << result(m, n) << " vs ref " << ref(m, n) << "\n";
        }
    }

    return 0;
}

// An invariant factor may be nested inside its own sub-product rather than
// sitting as one of a Mul's two immediate operands:
//   (scaleA(i) * castA) * (scaleB(i) * castB)
// -- the way two independently-scaled operands naturally compose. Finding both
// scales requires flattening the whole multiplicative chain into leaves and
// partitioning each one by invariance, not just checking a binary node's two
// immediate children.
int hoist_invariants_scattered_factors_test() {
    const int K = 64;
    ImageParam A{Int(8), 1, "A"};
    ImageParam B{Int(8), 1, "B"};
    ImageParam ScaleA{Float(32), 1, "ScaleA"};
    ImageParam ScaleB{Float(32), 1, "ScaleB"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += (cast<float>(A(r)) * ScaleA(i)) * (cast<float>(B(r)) * ScaleB(i));

    Func Acc_intm = Acc.update().hoist_invariants();
    internal_assert(Acc_intm.types()[0] == Float(32))
        << "hoist_invariants: expected the intermediate to keep Float(32), got "
        << Acc_intm.types()[0] << "\n";
    Acc_intm.compute_root();

    Buffer<int8_t> a_buf(K), b_buf(K);
    Buffer<float> scale_a_buf(1), scale_b_buf(1);
    for (int k = 0; k < K; k++) {
        a_buf(k) = 127;
        b_buf(k) = 127;
    }
    scale_a_buf(0) = 2.0f;
    scale_b_buf(0) = 3.0f;
    A.set(a_buf);
    B.set(b_buf);
    ScaleA.set(scale_a_buf);
    ScaleB.set(scale_b_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = 2.0f * 3.0f * (float)K * 127.0f * 127.0f;
    internal_assert(result(0) == expected)
        << "hoist_invariants scattered factors: got " << result(0) << ", expected " << expected << "\n";

    return 0;
}

// Same shape as hoist_invariants_scattered_factors_test, but with UInt(8)
// operands, to exercise the unsigned path.
int hoist_invariants_scattered_factors_unsigned_test() {
    const int K = 64;
    ImageParam A{UInt(8), 1, "A"};
    ImageParam B{UInt(8), 1, "B"};
    ImageParam ScaleA{Float(32), 1, "ScaleA"};
    ImageParam ScaleB{Float(32), 1, "ScaleB"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += (cast<float>(A(r)) * ScaleA(i)) * (cast<float>(B(r)) * ScaleB(i));

    Func Acc_intm = Acc.update().hoist_invariants();
    internal_assert(Acc_intm.types()[0] == Float(32))
        << "hoist_invariants: expected the intermediate to keep Float(32), got "
        << Acc_intm.types()[0] << "\n";
    Acc_intm.compute_root();

    Buffer<uint8_t> a_buf(K), b_buf(K);
    Buffer<float> scale_a_buf(1), scale_b_buf(1);
    for (int k = 0; k < K; k++) {
        a_buf(k) = 255;
        b_buf(k) = 255;
    }
    scale_a_buf(0) = 2.0f;
    scale_b_buf(0) = 3.0f;
    A.set(a_buf);
    B.set(b_buf);
    ScaleA.set(scale_a_buf);
    ScaleB.set(scale_b_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = 2.0f * 3.0f * (float)K * 255.0f * 255.0f;
    internal_assert(result(0) == expected)
        << "hoist_invariants scattered factors (unsigned): got " << result(0) << ", expected " << expected << "\n";

    return 0;
}

// A sum reduction can contain independently scaled terms. Each outer addend
// gets its own scalar intermediate, and the write-back applies the scale while
// merging those intermediates into the original accumulator.
int hoist_invariants_multiple_terms_test() {
    const int K = 16;
    RDom r(0, K, "r");

    Func f{"multiple_terms"};
    f() = 0;
    f() += 2 * (r + 1) + 3 * (2 * r + 1);

    FuncVec intms = f.update().hoist_invariants();
    internal_assert(intms.size() == 2)
        << "hoist_invariants multiple terms: expected two intermediates, got "
        << intms.size() << "\n";
    internal_assert(intms[0].name() == f.name() + "_intm0" &&
                    intms[1].name() == f.name() + "_intm1")
        << "hoist_invariants multiple terms: unexpected intermediate names\n";
    for (Func &intm : intms) {
        intm.compute_root();
    }

    Buffer<int> result = f.realize();
    int expected = 0;
    for (int k = 0; k < K; ++k) {
        expected += 2 * (k + 1) + 3 * (2 * k + 1);
    }
    internal_assert(result() == expected)
        << "hoist_invariants multiple terms: got " << result()
        << ", expected " << expected << "\n";
    return 0;
}

// Splitting outer terms is useful even when none has an invariant factor.
// Each term is reduced independently and merged at the original accumulator.
int hoist_invariants_unscaled_terms_test() {
    const int K = 16;
    Var x{"x"};
    RDom r(0, K, "r");

    Func g{"unscaled_g"}, h{"unscaled_h"}, f{"unscaled_terms"};
    g(x) = x + 1;
    h(x) = x * x + 3;
    f() = 0;
    f() += g(r) + h(r);

    FuncVec intms = f.update().hoist_invariants();
    internal_assert(intms.size() == 2)
        << "hoist_invariants unscaled terms: expected two intermediates, got "
        << intms.size() << "\n";
    for (Func &intm : intms) {
        intm.compute_root();
    }

    Buffer<int> result = f.realize();
    int expected = 0;
    for (int k = 0; k < K; ++k) {
        expected += (k + 1) + (k * k + 3);
    }
    internal_assert(result() == expected)
        << "hoist_invariants unscaled terms: got " << result()
        << ", expected " << expected << "\n";
    return 0;
}

// Tuple outputs are flattened independently. The FuncVec is ordered by tuple
// output index, then by the order of that output's flattened outer terms.
int hoist_invariants_tuple_terms_test() {
    const int K = 8;
    RDom r(0, K, "r");

    Func f{"tuple_terms"};
    f() = Tuple(0, 0);
    f() = Tuple(f()[0] + 2 * (r + 1) + 3 * (r + 2),
                f()[1] + 4 * (r + 3));

    FuncVec intms = f.update().hoist_invariants();
    internal_assert(intms.size() == 3)
        << "hoist_invariants tuple terms: expected three intermediates, got "
        << intms.size() << "\n";
    for (size_t i = 0; i < intms.size(); ++i) {
        internal_assert(intms[i].name() == f.name() + "_intm" + std::to_string(i))
            << "hoist_invariants tuple terms: unexpected intermediate ordering\n";
        intms[i].compute_root();
    }

    Realization result = f.realize();
    int expected0 = 0, expected1 = 0;
    for (int k = 0; k < K; ++k) {
        expected0 += 2 * (k + 1) + 3 * (k + 2);
        expected1 += 4 * (k + 3);
    }
    internal_assert(result[0].as<int>()() == expected0 &&
                    result[1].as<int>()() == expected1)
        << "hoist_invariants tuple terms: incorrect result\n";
    return 0;
}

// hoist_invariants() with outer Min and additive factor:
//   min_k(offset(i) + body(i, k)) = offset(i) + min_k(body(i, k))
// The intermediate accumulates min without the offset; write-back adds it once.
int hoist_invariants_min_test() {
    ImageParam offset_p{Float(32), 1, "offset_p"};
    ImageParam data_p{Float(32), 2, "data_p"};

    Var i{"i"};
    const int K = 16;
    RDom k(0, K, "k");

    Func C{"C"};
    C(i) = Float(32).max();
    C(i) = min(C(i), offset_p(i) + data_p(i, k));

    Func C_intm = C.update().hoist_invariants();
    C_intm.compute_root();

    const int M = 8;
    Buffer<float> off(M), dat(M, K);
    for (int m = 0; m < M; m++) {
        off(m) = (float)(m + 1);
        for (int kk = 0; kk < K; kk++) {
            dat(m, kk) = (float)(((m * K + kk) % 7) - 3);
        }
    }
    offset_p.set(off);
    data_p.set(dat);

    Buffer<float> result = C.realize({M});

    Buffer<float> ref(M);
    for (int m = 0; m < M; m++) {
        float v = std::numeric_limits<float>::max();
        for (int kk = 0; kk < K; kk++) {
            v = std::min(v, off(m) + dat(m, kk));
        }
        ref(m) = v;
    }

    for (int m = 0; m < M; m++) {
        internal_assert(result(m) == ref(m))
            << "hoist_invariants min mismatch at " << m << ": "
            << result(m) << " vs ref " << ref(m) << "\n";
    }
    return 0;
}

// hoist_invariants() with outer Or (bool) and And factor:
//   or_k(mask(i) && check(i, k)) = mask(i) && or_k(check(i, k))
// The intermediate accumulates or without the mask; write-back applies it once.
int hoist_invariants_or_test() {
    ImageParam mask_p{Bool(), 1, "mask_p"};
    ImageParam check_p{Bool(), 2, "check_p"};

    Var i{"i"};
    const int K = 16;
    RDom k(0, K, "k");

    Func valid{"valid"};
    valid(i) = cast<bool>(false);
    valid(i) = valid(i) || (mask_p(i) && check_p(i, k));

    Func valid_intm = valid.update().hoist_invariants();
    valid_intm.compute_root();

    const int M = 8;
    Buffer<bool> mask(M), chk(M, K);
    for (int m = 0; m < M; m++) {
        mask(m) = (m % 2 == 0);
        for (int kk = 0; kk < K; kk++) {
            chk(m, kk) = ((m + kk) % 3 == 0);
        }
    }
    mask_p.set(mask);
    check_p.set(chk);

    Buffer<bool> result = valid.realize({M});

    for (int m = 0; m < M; m++) {
        bool ref = false;
        for (int kk = 0; kk < K; kk++) {
            ref = ref || (mask(m) && chk(m, kk));
        }
        internal_assert(result(m) == ref)
            << "hoist_invariants or mismatch at " << m << ": "
            << (int)result(m) << " vs ref " << (int)ref << "\n";
    }
    return 0;
}

// hoist_invariants() must not disturb strict_float: the reduction below rounds
// each term to float before summing, and both the reference and the hoisted
// intermediate must observe that same rounding (giving exactly 0).
int hoist_invariants_strict_float_test() {
    Buffer<int32_t> data(2);
    data(0) = 16777217;
    data(1) = -16777216;

    RDom k(0, 2, "k");

    Func f{"f"};
    f() = 0.0f;
    f() += 1.5f * strict_float(cast<float>(data(k)));

    Func intm = f.update().hoist_invariants();
    intm.compute_root();

    internal_assert(intm.types()[0] == Float(32))
        << "hoist_invariants strict_float: expected intm to remain Float(32), got "
        << intm.types()[0] << "\n";

    Buffer<float> result = f.realize();
    internal_assert(result() == 0.0f)
        << "hoist_invariants strict_float mismatch: " << result()
        << " vs ref 0\n";

    return 0;
}

// A loop-invariant RDom predicate must be preserved on the write-back update.
// When enabled is false, the original reduction executes no iterations, so the
// hoisted factor (and its failing require) must not be evaluated.
int hoist_invariants_predicated_rdom_test() {
    Param<bool> enabled{"enabled"};
    RDom r(0, 4, "r");
    r.where(enabled);

    Func f{"f"};
    f() = 0;
    f() += require(enabled, 2, "hoisted factor evaluated outside RDom predicate") * (r + 1);

    Func intm = f.update().hoist_invariants();
    intm.compute_root();

    Module module = f.compile_to_module(f.infer_arguments(), "hoist_invariants_predicated_rdom");
    bool inside_predicate = false;
    bool found_writeback = false;
    for (const LoweredFunc &lowered_func : module.functions()) {
        visit_with(
            lowered_func.body,
            [&](auto *self, const IfThenElse *op) {
                const bool old_inside_predicate = inside_predicate;
                inside_predicate |= expr_uses_var(op->condition, enabled.name());
                (*self)(op->then_case);
                inside_predicate = old_inside_predicate;

                if (op->else_case.defined()) {
                    (*self)(op->else_case);
                }
            },
            [&](auto *self, const Store *op) {
                const bool predicated =
                    inside_predicate || expr_uses_var(op->predicate, enabled.name());
                if (op->name == f.name() && predicated) {
                    visit_with(op->value, [&](auto *, const Load *load) {
                        found_writeback |= load->name == intm.name();
                    });
                }
                self->visit_base(op);
            });
    }
    internal_assert(found_writeback)
        << "hoist_invariants predicated RDom: lowered pipeline had no write-back "
        << "guarded by " << enabled.name() << " that reads " << intm.name() << "\n";

    enabled.set(false);
    Buffer<int> disabled_result = f.realize();
    internal_assert(disabled_result() == 0)
        << "hoist_invariants predicated RDom: disabled result was "
        << disabled_result() << ", expected 0\n";

    enabled.set(true);
    Buffer<int> enabled_result = f.realize();
    internal_assert(enabled_result() == 20)
        << "hoist_invariants predicated RDom: enabled result was "
        << enabled_result() << ", expected 20\n";

    return 0;
}

// hoist_invariants() composes with rfactor(): rfactor first preserves r.y as a
// pure Var u, so scale(u) becomes invariant over the intermediate's remaining
// reduction over r.x and can then be hoisted from that intermediate.
int hoist_invariants_after_rfactor_test() {
    ImageParam scale_p{Float(32), 1, "scale_p"};
    ImageParam data_p{Int(32), 2, "data_p"};

    Var u{"u"};
    const int X = 8, Y = 4;
    RDom r(0, X, 0, Y, "r");

    Func f{"f"};
    f() = 0.0f;
    f() += scale_p(r.y) * data_p(r.x, r.y);

    // Preserve r.y as u; the intermediate now reduces only over r.x, and
    // scale_p(u) is invariant across that reduction.
    Func intm = f.update().rfactor({{r.y, u}});
    Func intm2 = intm.update().hoist_invariants();
    intm.compute_root();
    intm2.compute_root();

    Buffer<float> scale(Y);
    Buffer<int> data(X, Y);
    for (int y = 0; y < Y; y++) {
        scale(y) = (float)(y + 1);
        for (int x = 0; x < X; x++) {
            data(x, y) = (x + 2 * y) % 7 - 3;
        }
    }
    scale_p.set(scale);
    data_p.set(data);

    Buffer<float> result = f.realize();

    float ref = 0.0f;
    for (int y = 0; y < Y; y++) {
        int partial = 0;
        for (int x = 0; x < X; x++) {
            partial += data(x, y);
        }
        ref += scale(y) * (float)partial;
    }

    internal_assert(result() == ref)
        << "hoist_invariants after rfactor mismatch: "
        << result() << " vs ref " << ref << "\n";

    return 0;
}

// The min/max + add hoisting law is only valid for integer types where addition
// has no defined wraparound behavior. For UInt(8), hoisting the invariant 250
// would incorrectly turn min_k((250 + x_k) mod 256) into (250 + min_k(x_k)) mod
// 256, so hoist_invariants() must refuse rather than silently miscompile.
int hoist_invariants_invalid_law_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    Buffer<uint8_t> data(2);
    data(0) = 1;
    data(1) = 10;

    RDom k(0, 2, "k");

    Func f{"f"};
    f() = UInt(8).max();
    f() = min(f(), cast<uint8_t>(250) + data(k));

    bool error = false;
    try {
        f.update().hoist_invariants();
    } catch (const Halide::CompileError &e) {
        error = true;
        const string expected =
            "hoist_invariants() could not find multiple reduction terms or a "
            "distributable loop-invariant factor in the update definition of " +
            f.name() + ".";
        if (string(e.what()).find(expected) == string::npos) {
            printf("Unexpected error for unsigned min hoisting:\n%s\n", e.what());
            return 1;
        }
    }
    if (!error) {
        printf("hoist_invariants should have rejected the unsigned min law!\n");
        return 1;
    }
    return 0;
}

// hoist_invariants() errors when there is neither an invariant factor to hoist
// nor multiple outer terms to split, rather than behaving like plain rfactor().
int hoist_invariants_nothing_to_hoist_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    ImageParam data_p{Int(32), 2, "data_p"};
    Var i{"i"};
    RDom k(0, 8, "k");

    Func f{"f"};
    f(i) = 0;
    f(i) += data_p(i, k);

    bool error = false;
    try {
        f.update().hoist_invariants();
    } catch (const Halide::CompileError &e) {
        error = true;
        const string expected =
            "hoist_invariants() could not find multiple reduction terms or a "
            "distributable loop-invariant factor in the update definition of " +
            f.name() + ".";
        if (string(e.what()).find(expected) == string::npos) {
            printf("Unexpected error when no invariant is hoistable:\n%s\n", e.what());
            return 1;
        }
    }
    if (!error) {
        printf("hoist_invariants should have errored when there is nothing to hoist!\n");
        return 1;
    }
    return 0;
}

// An increment written as a sum of terms with *different* invariant factors has
// no single factor to hoist. Each term gets its own accumulator instead, all
// advanced by one loop, with the write-back applying each factor once.
int hoist_invariants_terms_test() {
    const int K = 64;
    ImageParam G{Int(8), 1, "G"};
    ImageParam H{Int(8), 1, "H"};
    ImageParam A{Float(32), 1, "A"};
    ImageParam B{Float(32), 1, "B"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += A(i) * cast<float>(G(r)) + B(i) * cast<float>(H(r));

    std::vector<Func> Acc_intm = Acc.update().hoist_invariants();
    internal_assert(Acc_intm.size() == 2)
        << "hoist_invariants terms: expected one accumulator per term, got "
        << Acc_intm.size() << "\n";
    for (Func &f : Acc_intm) {
        f.compute_root();
    }

    Buffer<int8_t> g_buf(K), h_buf(K);
    Buffer<float> a_buf(1), b_buf(1);
    for (int k = 0; k < K; k++) {
        g_buf(k) = 3;
        h_buf(k) = 5;
    }
    a_buf(0) = 2.0f;
    b_buf(0) = 7.0f;
    G.set(g_buf);
    H.set(h_buf);
    A.set(a_buf);
    B.set(b_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = (2.0f * 3.0f + 7.0f * 5.0f) * (float)K;
    internal_assert(result(0) == expected)
        << "hoist_invariants terms: got " << result(0) << ", expected " << expected << "\n";

    // Terms still get one accumulator each even when they share an identical
    // factor: hoist_invariants() doesn't try to detect and merge such terms,
    // so the accumulator count only ever depends on the number of terms.
    Func Shared{"Shared"};
    Shared(i) = 0.0f;
    Shared(i) += A(i) * cast<float>(G(r)) + A(i) * cast<float>(H(r));
    std::vector<Func> Shared_intm = Shared.update().hoist_invariants();
    internal_assert(Shared_intm.size() == 2)
        << "hoist_invariants terms: expected one accumulator per term even "
        << "when terms share a factor, got " << Shared_intm.size() << "\n";
    for (Func &f : Shared_intm) {
        f.compute_root();
    }

    Buffer<float> shared = Shared.realize({1});
    const float shared_expected = 2.0f * (3.0f + 5.0f) * (float)K;
    internal_assert(shared(0) == shared_expected)
        << "hoist_invariants shared factor: got " << shared(0) << ", expected "
        << shared_expected << "\n";

    return 0;
}

// distribute() multiplies a product of sums out so that hoist_invariants() can
// see terms that were not written as terms. This is the affine-quantized dot
// product: sum_k (d*q_k + m) * (e*p_k), whose expansion
// d*e*sum_k(q_k*p_k) + m*e*sum_k(p_k) has two integer-bodied accumulators where
// the unexpanded form has one float one.
int hoist_invariants_distribute_test() {
    const int K = 32;
    ImageParam Q{Int(8), 1, "Q"};
    ImageParam P{Int(8), 1, "P"};
    ImageParam D{Float(32), 1, "D"};
    ImageParam M{Float(32), 1, "M"};
    ImageParam E{Float(32), 1, "E"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += (cast<float>(Q(r)) * D(i) + M(i)) * (cast<float>(P(r)) * E(i));

    std::vector<Func> Acc_intm = Acc.update().distribute().hoist_invariants();
    internal_assert(Acc_intm.size() == 2)
        << "distribute: expected the multiplied-out increment to yield two "
        << "accumulators, got " << Acc_intm.size() << "\n";

    // Both bodies are integer sums of int8 products, so both retype -- and each
    // accumulator is its own Func, so each may take its own target type.
    Func qp = Acc_intm[0].change_type(Int(32));
    Func p_sum = Acc_intm[1].change_type(Int(16));
    internal_assert(qp.types()[0] == Int(32) && p_sum.types()[0] == Int(16))
        << "distribute: retyping the accumulators separately gave "
        << qp.types()[0] << " and " << p_sum.types()[0] << "\n";
    qp.compute_root();
    p_sum.compute_root();

    Buffer<int8_t> q_buf(K), p_buf(K);
    Buffer<float> d_buf(1), m_buf(1), e_buf(1);
    int64_t sum_qp = 0, sum_p = 0;
    for (int k = 0; k < K; k++) {
        q_buf(k) = (int8_t)(k % 15);
        p_buf(k) = (int8_t)((k * 5) % 127 - 63);
        sum_qp += (int64_t)q_buf(k) * p_buf(k);
        sum_p += p_buf(k);
    }
    d_buf(0) = 0.25f;
    m_buf(0) = -0.5f;
    e_buf(0) = 2.0f;
    Q.set(q_buf);
    P.set(p_buf);
    D.set(d_buf);
    M.set(m_buf);
    E.set(e_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = 0.25f * 2.0f * (float)sum_qp + -0.5f * 2.0f * (float)sum_p;
    internal_assert(std::abs(result(0) - expected) < 1e-3f)
        << "distribute: got " << result(0) << ", expected " << expected << "\n";

    return 0;
}

// distribute() is a schedule decision, so it says so when there is nothing to
// multiply out rather than quietly leaving the reduction alone.
int distribute_nothing_to_do_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }
    ImageParam A{Float(32), 1, "A"};
    Var i{"i"};
    RDom r(0, 8, "r");

    Func f{"f"};
    f(i) = 0.0f;
    f(i) += A(i) * cast<float>(r);

    try {
        f.update().distribute();
    } catch (const Halide::CompileError &e) {
        const std::string msg = e.what();
        internal_assert(msg.find("no product over a sum") != std::string::npos)
            << "distribute() rejected the update for the wrong reason: " << msg << "\n";
        return 0;
    }
    internal_assert(false) << "distribute() accepted an update with nothing to multiply out\n";
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        std::string desc;
        std::function<int()> fn;
    };

    std::vector<Task> tasks = {
        {"hoist_invariants test (add/mul)", hoist_invariants_test},
        {"hoist_invariants test (add/mul, scattered factors)", hoist_invariants_scattered_factors_test},
        {"hoist_invariants test (add/mul, scattered factors, unsigned)", hoist_invariants_scattered_factors_unsigned_test},
        {"hoist_invariants test (multiple terms)", hoist_invariants_multiple_terms_test},
        {"hoist_invariants test (unscaled terms)", hoist_invariants_unscaled_terms_test},
        {"hoist_invariants test (tuple terms)", hoist_invariants_tuple_terms_test},
        {"hoist_invariants test (min/add)", hoist_invariants_min_test},
        {"hoist_invariants test (or/and)", hoist_invariants_or_test},
        {"hoist_invariants test (strict_float preserved)", hoist_invariants_strict_float_test},
        {"hoist_invariants test (predicated RDom)", hoist_invariants_predicated_rdom_test},
        {"hoist_invariants test (after rfactor)", hoist_invariants_after_rfactor_test},
        {"hoist_invariants test (invalid law rejected)", hoist_invariants_invalid_law_rejected_test},
        {"hoist_invariants test (nothing to hoist rejected)", hoist_invariants_nothing_to_hoist_rejected_test},
        {"hoist_invariants test (one accumulator per term)", hoist_invariants_terms_test},
        {"distribute test (affine dot product)", hoist_invariants_distribute_test},
        {"distribute test (nothing to distribute rejected)", distribute_nothing_to_do_rejected_test},
    };

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        std::cout << task.desc << "\n";
        if (task.fn() != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
