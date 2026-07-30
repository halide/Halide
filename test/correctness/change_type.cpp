#include "Halide.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

using namespace Halide;

namespace {

// TODO: add tests that compose change_type() with hoist_invariants() -- e.g.
// retyping the float dot-product intermediate hoist_invariants() returns to an
// Int(32) accumulator, and confirming a min-reduction retype uses the
// reduction identity at the new type rather than a lossy cast of the original
// float identity.

// A symbolic reduction extent can't be bounded at schedule time, so change_type
// injects a runtime precondition. With a valid (small) extent it passes and the
// result is correct.
int change_type_symbolic_extent_test() {
    ImageParam A{Int(8), 1, "A"}, B{Int(8), 1, "B"};

    Var i{"i"};
    // The extent is a runtime value (an ImageParam dimension), so it can't be
    // bounded at schedule time.
    RDom r(0, A.dim(0).extent(), "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += cast<float>(widening_mul(A(r), B(r)));

    // int8*int8 accumulated over a symbolic number of terms: change_type injects
    // a runtime precondition guaranteeing the sum fits in Int(32).
    Func Acc_i32 = Acc.change_type(Int(32));
    internal_assert(Acc_i32.types()[0] == Int(32))
        << "change_type symbolic: expected Int(32), got " << Acc_i32.types()[0] << "\n";
    Acc_i32.compute_root();

    const int K = 100;
    Buffer<int8_t> a(K), b(K);
    for (int k = 0; k < K; k++) {
        a(k) = (int8_t)((k % 9) - 4);
        b(k) = (int8_t)((k % 7) - 3);
    }
    A.set(a);
    B.set(b);

    Buffer<float> result = Acc.realize({4});
    int32_t dot = 0;
    for (int k = 0; k < K; k++) {
        dot += (int32_t)a(k) * (int32_t)b(k);
    }
    for (int m = 0; m < 4; m++) {
        float expected = (float)dot;
        internal_assert(result(m) == expected)
            << "change_type symbolic-extent mismatch at " << m << ": " << result(m)
            << " vs " << expected << "\n";
    }
    return 0;
}

// change_type() can be applied more than once, retyping the intermediate
// returned by a previous change_type(). Each step must remain safe and correct.
int change_type_twice_test() {
    const int K = 32;
    ImageParam A{Int(8), 1, "A"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    // Sum of K int8 values: |sum| <= 32 * 127 = 4064, which fits Int(16), so both
    // retypes (Float(32) -> Int(32) -> Int(16)) are statically safe.
    Acc(i) += cast<float>(A(r));

    Func Acc_i32 = Acc.change_type(Int(32));
    Func Acc_i16 = Acc_i32.change_type(Int(16));
    internal_assert(Acc.types()[0] == Float(32) &&
                    Acc_i32.types()[0] == Int(32) &&
                    Acc_i16.types()[0] == Int(16))
        << "change_type twice: unexpected types "
        << Acc.types()[0] << " / " << Acc_i32.types()[0] << " / " << Acc_i16.types()[0] << "\n";
    Acc_i16.compute_root();
    Acc_i32.compute_root();

    Buffer<int8_t> a(K);
    for (int k = 0; k < K; k++) {
        a(k) = (int8_t)((k % 15) - 7);
    }
    A.set(a);

    Buffer<float> result = Acc.realize({2});
    int32_t sum = 0;
    for (int k = 0; k < K; k++) {
        sum += (int32_t)a(k);
    }
    for (int m = 0; m < 2; m++) {
        internal_assert(result(m) == (float)sum)
            << "change_type twice mismatch at " << m << ": " << result(m) << " vs " << sum << "\n";
    }
    return 0;
}

// A statically-sized accumulation whose per-term range is only small enough to
// fit the target type because an upstream producer clamps its value. Without the
// producer's proven bounds, change_type() sees the term's full type range and
// (correctly) rejects the retype as an overflow risk. This test therefore relies
// on change_type() consulting FuncValueBounds, and checks the result is correct.
int change_type_producer_bounds_test() {
    const int K = 1000;
    ImageParam A{Int(16), 1, "A"};

    Var i{"i"}, x{"x"};
    RDom r(0, K, "r");

    // clamp(A, 0, 10) has a proven value range of [0, 10], so a sum of K of them
    // is at most 10 * 1000 = 10000, which fits Int(16). A raw Int(16) term would
    // span [-32768, 32767], and K of those would blow past Int(16).
    Func p{"p"};
    p(x) = clamp(A(x), 0, 10);

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += cast<float>(p(r));

    Func Acc_i16 = Acc.change_type(Int(16));
    internal_assert(Acc_i16.types()[0] == Int(16))
        << "change_type producer-bounds: expected Int(16), got " << Acc_i16.types()[0] << "\n";
    p.compute_root();
    Acc_i16.compute_root();

    Buffer<int16_t> a(K);
    for (int k = 0; k < K; k++) {
        // Spread values well outside [0, 10] so the clamp actually bites and a
        // missing clamp would give a different (and overflowing) answer.
        a(k) = (int16_t)(((k * 37) % 400) - 150);
    }
    A.set(a);

    Buffer<float> result = Acc.realize({4});
    int32_t sum = 0;
    for (int k = 0; k < K; k++) {
        sum += std::min(std::max((int32_t)a(k), 0), 10);
    }
    for (int m = 0; m < 4; m++) {
        internal_assert(result(m) == (float)sum)
            << "change_type producer-bounds mismatch at " << m << ": " << result(m)
            << " vs " << sum << "\n";
    }

#if HALIDE_WITH_EXCEPTIONS
    // The same reduction without the clamp is a genuine overflow risk: a sum of K
    // raw Int(16) terms does not fit Int(16). change_type() must reject it, which
    // confirms the test above passed because of the producer's bounds and not for
    // some unrelated reason.
    if (Halide::exceptions_enabled()) {
        ImageParam B{Int(16), 1, "B"};
        Func q{"q"};
        q(x) = B(x);  // no clamp -> full Int(16) value range

        Func Acc2{"Acc2"};
        Acc2(i) = 0.0f;
        Acc2(i) += cast<float>(q(r));

        bool threw = false;
        try {
            Acc2.change_type(Int(16));
        } catch (const Halide::CompileError &) {
            threw = true;
        }
        internal_assert(threw)
            << "change_type without producer bounds should have been rejected as an overflow risk\n";
    }
#endif

    return 0;
}

// A dot product of two clamped producers. The widening_mul term is only provably
// within Int(32) because both producers are clamped, and this also exercises the
// retype_leaf/lossless_cast interaction: the float-wrapped widening_mul must come
// back as an integer widening_mul (no float round-trip) at the new type.
int change_type_bounded_dot_product_test() {
    const int K = 8;
    ImageParam A{Int(16), 1, "A"}, B{Int(16), 1, "B"};

    Var i{"i"}, x{"x"};
    RDom r(0, K, "r");

    Func pa{"pa"}, pb{"pb"};
    pa(x) = clamp(A(x), 0, 100);
    pb(x) = clamp(B(x), 0, 100);

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += cast<float>(pa(r)) * cast<float>(pb(r));

    // Per-term product is at most 100 * 100 = 10000; over K = 8 terms that is at
    // most 80000, comfortably inside Int(32). A raw Int(16) x Int(16) product
    // spans up to ~2^30, and K of those would overflow Int(32).
    Func Acc_i32 = Acc.change_type(Int(32));
    internal_assert(Acc_i32.types()[0] == Int(32))
        << "change_type bounded dot-product: expected Int(32), got " << Acc_i32.types()[0] << "\n";
    pa.compute_root();
    pb.compute_root();
    Acc_i32.compute_root();

    Buffer<int16_t> a(K), b(K);
    for (int k = 0; k < K; k++) {
        a(k) = (int16_t)((k * 53) % 500 - 200);
        b(k) = (int16_t)((k * 71) % 500 - 200);
    }
    A.set(a);
    B.set(b);

    Buffer<float> result = Acc.realize({4});
    int32_t dot = 0;
    for (int k = 0; k < K; k++) {
        int32_t ca = std::min(std::max((int32_t)a(k), 0), 100);
        int32_t cb = std::min(std::max((int32_t)b(k), 0), 100);
        dot += ca * cb;
    }
    for (int m = 0; m < 4; m++) {
        internal_assert(result(m) == (float)dot)
            << "change_type bounded dot-product mismatch at " << m << ": " << result(m)
            << " vs " << dot << "\n";
    }
    return 0;
}

// change_type() on a pure Func that narrows to a type its values fit only after
// a clamp. lossless_cast() can't prove the narrowing (it can't push the cast into
// the min(A, 100) subterm, whose range spills below Int(8)), so retype_leaf()
// falls back to a plain cast -- which is nonetheless exact here because the clamp
// keeps every value in [0, 100]. This exercises the retype_leaf() fallback path.
int change_type_pure_narrowing_test() {
    ImageParam A{Int(16), 1, "A"};

    Var x{"x"};
    Func f{"f"};
    f(x) = clamp(A(x), 0, 100);

    Func f_i8 = f.change_type(Int(8));
    internal_assert(f_i8.types()[0] == Int(8))
        << "change_type pure-narrowing: expected Int(8), got " << f_i8.types()[0] << "\n";
    f_i8.compute_root();

    const int W = 64;
    Buffer<int16_t> a(W);
    for (int j = 0; j < W; j++) {
        a(j) = (int16_t)(((j * 41) % 600) - 250);  // spans well past [0, 100]
    }
    A.set(a);

    Buffer<int16_t> result = f.realize({W});
    for (int j = 0; j < W; j++) {
        int16_t expected = (int16_t)std::min(std::max((int)a(j), 0), 100);
        internal_assert(result(j) == expected)
            << "change_type pure-narrowing mismatch at " << j << ": " << result(j)
            << " vs " << expected << "\n";
    }
    return 0;
}

// A dot product of two clamped producers retyped to Int(16) -- narrower than the
// natural Int(32) product. The producer bounds do two things here. For
// acceptance: the overflow proof only fits the K-term accumulator in Int(16)
// because each product is in [0, 10000] (a raw Int(16)^2 term would blow past
// Int(16)); without the clamps change_type() rejects the retype. For code
// quality: seeding those bounds into lossless_cast() lets it push the cast into
// the widening_mul and recover a narrow Int(16) multiply rather than falling back
// to a cast of the Int(32) product. That second effect is not observable here --
// the fallback is value-equal whenever the product fits Int(16), so only the
// generated IR differs -- but this exercises that lossless_cast() path.
int change_type_narrowing_dot_product_test() {
    const int K = 3;
    ImageParam A{Int(16), 1, "A"}, B{Int(16), 1, "B"};

    Var i{"i"}, x{"x"};
    RDom r(0, K, "r");

    Func pa{"pa"}, pb{"pb"};
    pa(x) = clamp(A(x), 0, 100);
    pb(x) = clamp(B(x), 0, 100);

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += cast<float>(pa(r)) * cast<float>(pb(r));

    // Per-term product <= 10000; over K = 3 terms the sum <= 30000, which fits
    // Int(16). The per-term bound is what lets the widening_mul narrow to Int(16).
    Func Acc_i16 = Acc.change_type(Int(16));
    internal_assert(Acc_i16.types()[0] == Int(16))
        << "change_type narrowing dot-product: expected Int(16), got " << Acc_i16.types()[0] << "\n";
    pa.compute_root();
    pb.compute_root();
    Acc_i16.compute_root();

    Buffer<int16_t> a(K), b(K);
    for (int k = 0; k < K; k++) {
        a(k) = (int16_t)((k * 53) % 500 - 200);
        b(k) = (int16_t)((k * 71) % 500 - 200);
    }
    A.set(a);
    B.set(b);

    Buffer<float> result = Acc.realize({4});
    int32_t dot = 0;
    for (int k = 0; k < K; k++) {
        int32_t ca = std::min(std::max((int32_t)a(k), 0), 100);
        int32_t cb = std::min(std::max((int32_t)b(k), 0), 100);
        dot += ca * cb;
    }
    for (int m = 0; m < 4; m++) {
        internal_assert(result(m) == (float)dot)
            << "change_type narrowing dot-product mismatch at " << m << ": " << result(m)
            << " vs " << dot << "\n";
    }
    return 0;
}

// A narrowing change_type() that can't be proven exact must be rejected rather
// than silently truncating -- unless the caller opts in with unsafe = true. This
// covers both the pure path and the min/max reduction path, whose per-term casts
// would otherwise clamp their own bounds and hide the truncation.
int change_type_truncating_rejected_test() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        return 0;
    }
    Var i{"i"}, x{"x"};

    // Pure narrowing of an unbounded Int(16) value to Int(8): not representable.
    {
        ImageParam A{Int(16), 1, "A"};
        Func f{"f_trunc"};
        f(x) = A(x);

        bool threw = false;
        try {
            f.change_type(Int(8));
        } catch (const Halide::CompileError &) {
            threw = true;
        }
        internal_assert(threw)
            << "change_type(Int(8)) on an unbounded Int(16) value should be rejected as truncating\n";

        // With unsafe = true the caller takes responsibility and it is allowed.
        Func g{"g_trunc"};
        g(x) = A(x);
        Func g_i8 = g.change_type(Int(8), /*unsafe*/ true);
        internal_assert(g_i8.types()[0] == Int(8))
            << "unsafe change_type should proceed despite possible truncation\n";
    }

    // A max-reduction over an unbounded Int(16) term narrowed to Int(8): the term
    // itself may not be representable, so it must be rejected too.
    {
        ImageParam A{Int(16), 1, "A"};
        RDom r(0, 8, "r");
        Func m{"m_trunc"};
        m(i) = cast<float>(A(0));
        m(i) = max(m(i), cast<float>(A(r)));

        bool threw = false;
        try {
            m.change_type(Int(8));
        } catch (const Halide::CompileError &) {
            threw = true;
        }
        internal_assert(threw)
            << "change_type(Int(8)) on an unbounded max-reduction term should be rejected as truncating\n";
    }
#endif
    return 0;
}

// Code-quality regression: when producer bounds let lossless_cast() narrow the
// dot-product term, it should push the cast into the multiply and leave a native
// Int(16) multiply, not a cast of an Int(32) widening_mul. This is what the cache
// seeded into lossless_cast() buys; without it the retype still computes the right
// answer but emits the wider multiply, so a correctness test can't catch its loss.
int change_type_keeps_narrow_multiply_test() {
    const int K = 3;
    ImageParam A{Int(16), 1, "A"}, B{Int(16), 1, "B"};

    Var i{"i"}, x{"x"};
    RDom r(0, K, "r");

    Func pa{"pa"}, pb{"pb"};
    pa(x) = clamp(A(x), 0, 100);
    pb(x) = clamp(B(x), 0, 100);

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += cast<float>(pa(r)) * cast<float>(pb(r));

    Func Acc_i16 = Acc.change_type(Int(16));

    // Inspect the retyped update expression directly (before lowering, so no later
    // pass can reintroduce or erase the intrinsic).
    std::ostringstream os;
    os << Acc_i16.update_value(0);
    const std::string retyped = os.str();
    internal_assert(retyped.find("widening_mul") == std::string::npos)
        << "change_type() should push the narrowing cast into the multiply, leaving a "
        << "native Int(16) multiply, but the retyped term kept a widening_mul:\n"
        << retyped << "\n";
    return 0;
}

// A product reduction grows the accumulator multiplicatively, which the overflow
// proof does not model, so change_type() rejects it (a product of K terms that
// each fit the target type can still be term^K, far past its range). Support may
// be added later; until then it must fail loudly rather than silently overflow.
int change_type_product_reduction_unsupported_test() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        return 0;
    }
    ImageParam A{Int(8), 1, "A"};
    Var i{"i"};
    RDom r(0, 8, "r");

    Func Acc{"Acc"};
    Acc(i) = 1.0f;
    Acc(i) *= cast<float>(A(r));  // product reduction

    bool threw = false;
    try {
        Acc.change_type(Int(32));
    } catch (const Halide::CompileError &) {
        threw = true;
    }
    internal_assert(threw)
        << "change_type() should reject a product reduction as unsupported\n";
#endif
    return 0;
}

// A difference reduction subtracts each term, which grows the accumulator
// additively (by -term), so change_type() supports it with the same bound as a
// sum. Here -sum of K int8 values fits Int(16).
int change_type_difference_reduction_test() {
    const int K = 100;
    ImageParam A{Int(8), 1, "A"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) -= cast<float>(A(r));  // difference reduction: -sum of int8

    Func Acc_i16 = Acc.change_type(Int(16));
    internal_assert(Acc_i16.types()[0] == Int(16))
        << "change_type difference: expected Int(16), got " << Acc_i16.types()[0] << "\n";
    Acc_i16.compute_root();

    Buffer<int8_t> a(K);
    for (int k = 0; k < K; k++) {
        a(k) = (int8_t)((k * 31) % 255 - 127);  // spans the full int8 range
    }
    A.set(a);

    Buffer<float> result = Acc.realize({4});
    int32_t neg_sum = 0;
    for (int k = 0; k < K; k++) {
        neg_sum -= (int32_t)a(k);
    }
    for (int m = 0; m < 4; m++) {
        internal_assert(result(m) == (float)neg_sum)
            << "change_type difference mismatch at " << m << ": " << result(m)
            << " vs " << neg_sum << "\n";
    }
    return 0;
}

// The widening fold is keyed on float-exact-representability, not a fixed table
// of 8/16-bit Float(32) patterns, so it also fires for int32 operands under a
// Float(64) accumulator. A dot product of two clamped int32 producers retyped to
// Int(64) should therefore come back as an integer widening_mul(i32, i32).
int change_type_widening_fold_generalizes_test() {
    const int K = 4;
    ImageParam A{Int(32), 1, "A"}, B{Int(32), 1, "B"};

    Var i{"i"}, x{"x"};
    RDom r(0, K, "r");

    Func pa{"pa"}, pb{"pb"};
    pa(x) = clamp(A(x), 0, 1000);
    pb(x) = clamp(B(x), 0, 1000);

    Func Acc{"Acc"};
    Acc(i) = cast<double>(0);
    Acc(i) += cast<double>(pa(r)) * cast<double>(pb(r));

    // Per-term product <= 1e6; over K = 4 terms the sum <= 4e6, well inside
    // Int(64). The int32 operands round-trip through Float(64) exactly, so the
    // fold applies even though the old table only covered 8/16-bit under f32.
    Func Acc_i64 = Acc.change_type(Int(64));
    internal_assert(Acc_i64.types()[0] == Int(64))
        << "change_type widening-fold: expected Int(64), got " << Acc_i64.types()[0] << "\n";

    std::ostringstream os;
    os << Acc_i64.update_value(0);
    const std::string retyped = os.str();
    internal_assert(retyped.find("widening_mul") != std::string::npos)
        << "change_type() should expose an integer widening_mul for int32-under-f64, "
        << "but the retyped term was:\n"
        << retyped << "\n";

    pa.compute_root();
    pb.compute_root();
    Acc_i64.compute_root();

    Buffer<int32_t> a(K), b(K);
    for (int k = 0; k < K; k++) {
        a(k) = (k * 811) % 3000 - 1000;
        b(k) = (k * 977) % 3000 - 1000;
    }
    A.set(a);
    B.set(b);

    Buffer<double> result = Acc.realize({4});
    int64_t dot = 0;
    for (int k = 0; k < K; k++) {
        int64_t ca = std::min(std::max(a(k), 0), 1000);
        int64_t cb = std::min(std::max(b(k), 0), 1000);
        dot += ca * cb;
    }
    for (int m = 0; m < 4; m++) {
        internal_assert(result(m) == (double)dot)
            << "change_type widening-fold mismatch at " << m << ": " << result(m)
            << " vs " << dot << "\n";
    }
    return 0;
}

// The overflow proof must include the initial value as well as the reduction
// terms. Although ten increments of one fit in Int(8), starting from 120 makes
// the final value 130, which does not.
int change_type_initial_value_contributes_to_overflow_test() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    ImageParam A{UInt(1), 1, "A_seed_overflow"};
    Var i{"i"};
    RDom r(0, 10, "r");

    Func Acc{"Acc_seed_overflow"};
    Acc(i) = 120.0f;
    Acc(i) += cast<float>(A(r));

    bool threw = false;
    try {
        Acc.change_type(Int(8));
    } catch (const Halide::CompileError &) {
        threw = true;
    }
    internal_assert(threw)
        << "change_type(Int(8)) should reject 120 + ten increments of one: "
        << "the initial value makes the accumulator overflow\n";
#endif
    return 0;
}

// The accumulator range must flow from one update stage into the next. Each
// stage adds only 100, which fits Int(8) in isolation, but together they add 200.
int change_type_multiple_updates_accumulate_overflow_test() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    ImageParam A{UInt(1), 1, "A_multi_update_1"};
    ImageParam B{UInt(1), 1, "B_multi_update_2"};
    Var i{"i"};
    RDom r1(0, 100, "r1"), r2(0, 100, "r2");

    Func Acc{"Acc_multi_update_overflow"};
    Acc(i) = 0.0f;
    Acc(i) += cast<float>(A(r1));
    Acc(i) += cast<float>(B(r2));

    bool threw = false;
    try {
        Acc.change_type(Int(8));
    } catch (const Halide::CompileError &) {
        threw = true;
    }
    internal_assert(threw)
        << "change_type(Int(8)) should reject two update stages that cumulatively "
        << "add 200, even though each stage adds only 100\n";
#endif
    return 0;
}

// Looking only at the top-level '+' is not enough to classify an update as a
// sum reduction: the self-containing branch can apply another recurrence. This
// update grows as 2*x + 1 and reaches 255 after eight iterations.
int change_type_nested_accumulator_recurrence_rejected_test() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    ImageParam A{UInt(1), 1, "A_nested_recurrence"};
    Var i{"i"};
    RDom r(0, 8, "r");

    Func Acc{"Acc_nested_recurrence"};
    Acc(i) = 0.0f;
    Acc(i) = Acc(i) * 2.0f + cast<float>(A(r));

    bool threw = false;
    try {
        Acc.change_type(Int(8));
    } catch (const Halide::CompileError &) {
        threw = true;
    }
    internal_assert(threw)
        << "change_type(Int(8)) should reject a nested 2*x + 1 recurrence instead "
        << "of treating it as a sum of eight ones\n";
#endif
    return 0;
}

// A min/max seed is not necessarily the operator identity. Retyping must
// preserve a finite seed rather than unconditionally replacing it with the
// target type's identity.
int change_type_min_preserves_non_identity_seed_test() {
    const int K = 4;
    ImageParam A{Int(8), 1, "A_min_seed"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Min{"Min_non_identity_seed"};
    Min(i) = 5.0f;
    Min(i) = min(Min(i), cast<float>(A(r)));

    Func Min_i8 = Min.change_type(Int(8));
    Min_i8.compute_root();

    Buffer<int8_t> a(K);
    for (int k = 0; k < K; k++) {
        a(k) = (int8_t)(10 + k);
    }
    A.set(a);

    Buffer<float> result = Min.realize({1});
    internal_assert(result(0) == 5.0f)
        << "change_type() changed a min reduction's seed from 5 to the Int(8) "
        << "identity; result was " << result(0) << " instead of 5\n";
    return 0;
}

// The non-identity case above must not regress the intended special handling
// for a true floating-point min identity, which cannot be cast directly to an
// integer target without losing its identity semantics.
int change_type_min_translates_identity_seed_test() {
    const int K = 4;
    ImageParam A{Int(8), 1, "A_min_identity"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Min{"Min_identity_seed"};
    Min(i) = Float(32).max();
    Min(i) = min(Min(i), cast<float>(A(r)));

    Func Min_i8 = Min.change_type(Int(8));
    Min_i8.compute_root();

    Buffer<int8_t> a(K);
    for (int k = 0; k < K; k++) {
        a(k) = (int8_t)(10 + k);
    }
    A.set(a);

    Buffer<float> result = Min.realize({1});
    internal_assert(result(0) == 10.0f)
        << "change_type() did not translate the Float(32) min identity to the "
        << "Int(8) identity; result was " << result(0) << " instead of 10\n";
    return 0;
}

// Translating an identity that does not round-trip through the target type is
// only sound when the first update is guaranteed to replace it at every pure
// coordinate. Constant empty domains are rejected, symbolic domains get a
// runtime non-empty check, and scatter or predicated updates are rejected.
int change_type_identity_translation_requires_dense_nonempty_update_test() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    // A statically empty dense reduction would expose the translated Int(8)
    // identity (127) instead of the original Float(32) identity (+infinity).
    {
        Var i{"i"};
        RDom r(0, 0, "r_static_empty");
        Func Min{"Min_static_empty"};
        Min(i) = Float(32).max();
        Min(i) = min(Min(i), cast<float>(r % 2));

        bool threw = false;
        try {
            Min.change_type(Int(8));
        } catch (const Halide::CompileError &) {
            threw = true;
        }
        internal_assert(threw)
            << "change_type() should reject identity translation for a statically "
            << "empty reduction domain\n";
    }

    // A symbolic dense reduction is allowed, but zero must fail its generated
    // runtime precondition while a positive extent still computes normally.
    {
        Param<int32_t> extent{"identity_extent"};
        Var i{"i"};
        RDom r(0, extent, "r_symbolic_empty");
        Func Min{"Min_symbolic_empty"};
        Min(i) = Float(32).max();
        Min(i) = min(Min(i), cast<float>(r % 2));

        Func Min_i8 = Min.change_type(Int(8));
        Min_i8.compute_root();

        extent.set(0);
        bool threw = false;
        try {
            (void)Min.realize({1});
        } catch (const Halide::RuntimeError &) {
            threw = true;
        }
        internal_assert(threw)
            << "change_type() should require a symbolic reduction domain to be non-empty "
            << "when translating an identity\n";

        extent.set(1);
        Buffer<float> result = Min.realize({1});
        internal_assert(result(0) == 0.0f);
    }

    // A non-empty scatter domain does not update every pure coordinate.
    {
        Var x{"x"};
        RDom r(0, 4, "r_scatter");
        Func Min{"Min_scatter_identity"};
        Min(x) = Float(32).max();
        Min(r) = min(Min(r), cast<float>(r % 2));

        bool threw = false;
        try {
            Min.change_type(Int(8));
        } catch (const Halide::CompileError &) {
            threw = true;
        }
        internal_assert(threw)
            << "change_type() should reject identity translation for a scatter update\n";
    }

    // A predicate can filter out every reduction point for an output.
    {
        Var i{"i"};
        RDom r(0, 4, "r_predicated");
        r.where(r < 2);
        Func Min{"Min_predicated_identity"};
        Min(i) = Float(32).max();
        Min(i) = min(Min(i), cast<float>(r % 2));

        bool threw = false;
        try {
            Min.change_type(Int(8));
        } catch (const Halide::CompileError &) {
            threw = true;
        }
        internal_assert(threw)
            << "change_type() should reject identity translation for a predicated update\n";
    }
#endif
    return 0;
}

// A runtime guard installed by an earlier change_type() must survive a later
// retype. The first step requires the symbolic reduction to fit Int(16); the
// second step widens the actual accumulator to Int(32), but its cast-back wrapper
// still narrows the result through Int(16).
int change_type_chaining_preserves_runtime_checks_test() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    Param<int32_t> extent{"chain_extent"};
    ImageParam A{Int(8), 1, "A_chain_checks"};

    Var i{"i"};
    RDom r(0, extent, "r");

    Func Acc{"Acc_chain_checks"};
    Acc(i) = 0.0f;
    Acc(i) += cast<float>(A(r));

    Func Acc_i16 = Acc.change_type(Int(16));
    Func Acc_i32 = Acc_i16.change_type(Int(32));
    Acc_i32.compute_root();

    const int K = 300;
    Buffer<int8_t> a(K);
    a.fill(127);
    A.set(a);
    extent.set(K);

    bool threw = false;
    try {
        (void)Acc.realize({1});
    } catch (const Halide::RuntimeError &) {
        threw = true;
    }
    internal_assert(threw)
        << "the Int(16) overflow guard was lost after chaining change_type(Int(32)); "
        << "a sum of 300 * 127 should have failed at runtime\n";
#endif
    return 0;
}

// The runtime guard itself must not use wrapping arithmetic. For a full-range
// Int(64) term, multiplying either endpoint by a symbolic extent of two wraps,
// making both comparisons spuriously true even though the accumulation can
// overflow.
int change_type_runtime_check_arithmetic_does_not_overflow_test() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    Param<int32_t> extent{"guard_extent"};
    ImageParam A{Int(64), 1, "A_guard_overflow"};

    Var i{"i"};
    RDom r(0, extent, "r");

    Func Acc{"Acc_guard_overflow"};
    Acc(i) = cast<double>(0);
    Acc(i) += cast<double>(A(r));

    Func Acc_i64 = Acc.change_type(Int(64));
    Acc_i64.compute_root();

    Buffer<int64_t> a(2);
    a.fill(int64_t{1} << 62);
    A.set(a);

    // One full-range term is permitted and must not be rejected by an overly
    // conservative or malformed guard.
    extent.set(1);
    Buffer<double> safe_result = Acc.realize({1});
    internal_assert(safe_result(0) == (double)(int64_t{1} << 62));

    // Two such terms may overflow Int(64), so the guard must reject the extent
    // before the reduction executes.
    extent.set(2);

    bool threw = false;
    try {
        (void)Acc.realize({1});
    } catch (const Halide::RuntimeError &) {
        threw = true;
    }
    internal_assert(threw)
        << "the change_type(Int(64)) guard overflowed while checking a symbolic "
        << "extent of two and failed to reject an overflowing accumulation\n";
#endif
    return 0;
}

// The compile-time term count must also use checked arithmetic. Three extents
// of 2^22 have a product of 2^66; the unchecked signed multiplication is
// undefined and, in this case, makes an enormous reduction look harmless.
int change_type_static_extent_count_does_not_overflow_test() {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    constexpr int extent = 1 << 22;
    RDom r({{0, extent}, {0, extent}, {0, extent}}, "r");

    Func Acc{"Acc_static_extent_overflow"};
    Acc() = 0.0f;
    // Keep all three RVars live without requiring an impossibly large buffer.
    // Each loop extent is legal on its own, and a scalar reduction has no
    // allocation proportional to the product of its reduction extents.
    Acc() += cast<float>((r.x == 0) && (r.y == 0) && (r.z == 0));

    bool threw = false;
    try {
        Acc.change_type(Int(8));
    } catch (const Halide::CompileError &) {
        threw = true;
    }
    internal_assert(threw)
        << "change_type(Int(8)) should reject a 2^66-term reduction; its static "
        << "term-count calculation overflowed and made the reduction appear safe\n";
#endif
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    printf("Running change_type_symbolic_extent_test\n");
    if (change_type_symbolic_extent_test()) {
        return 1;
    }
    printf("Running change_type_twice_test\n");
    if (change_type_twice_test()) {
        return 1;
    }
    printf("Running change_type_producer_bounds_test\n");
    if (change_type_producer_bounds_test()) {
        return 1;
    }
    printf("Running change_type_bounded_dot_product_test\n");
    if (change_type_bounded_dot_product_test()) {
        return 1;
    }
    printf("Running change_type_pure_narrowing_test\n");
    if (change_type_pure_narrowing_test()) {
        return 1;
    }
    printf("Running change_type_narrowing_dot_product_test\n");
    if (change_type_narrowing_dot_product_test()) {
        return 1;
    }
    printf("Running change_type_truncating_rejected_test\n");
    if (change_type_truncating_rejected_test()) {
        return 1;
    }
    printf("Running change_type_keeps_narrow_multiply_test\n");
    if (change_type_keeps_narrow_multiply_test()) {
        return 1;
    }
    printf("Running change_type_product_reduction_unsupported_test\n");
    if (change_type_product_reduction_unsupported_test()) {
        return 1;
    }
    printf("Running change_type_difference_reduction_test\n");
    if (change_type_difference_reduction_test()) {
        return 1;
    }
    printf("Running change_type_widening_fold_generalizes_test\n");
    if (change_type_widening_fold_generalizes_test()) {
        return 1;
    }
    printf("Running change_type_initial_value_contributes_to_overflow_test\n");
    if (change_type_initial_value_contributes_to_overflow_test()) {
        return 1;
    }
    printf("Running change_type_multiple_updates_accumulate_overflow_test\n");
    if (change_type_multiple_updates_accumulate_overflow_test()) {
        return 1;
    }
    printf("Running change_type_nested_accumulator_recurrence_rejected_test\n");
    if (change_type_nested_accumulator_recurrence_rejected_test()) {
        return 1;
    }
    printf("Running change_type_min_preserves_non_identity_seed_test\n");
    if (change_type_min_preserves_non_identity_seed_test()) {
        return 1;
    }
    printf("Running change_type_min_translates_identity_seed_test\n");
    if (change_type_min_translates_identity_seed_test()) {
        return 1;
    }
    printf("Running change_type_identity_translation_requires_dense_nonempty_update_test\n");
    if (change_type_identity_translation_requires_dense_nonempty_update_test()) {
        return 1;
    }
    printf("Running change_type_chaining_preserves_runtime_checks_test\n");
    if (change_type_chaining_preserves_runtime_checks_test()) {
        return 1;
    }
    printf("Running change_type_runtime_check_arithmetic_does_not_overflow_test\n");
    if (change_type_runtime_check_arithmetic_does_not_overflow_test()) {
        return 1;
    }
    printf("Running change_type_static_extent_count_does_not_overflow_test\n");
    if (change_type_static_extent_count_does_not_overflow_test()) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
