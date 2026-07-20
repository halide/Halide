#include "Halide.h"
#include <cstdio>

using namespace Halide;

namespace {

// change_type() retypes a Func's accumulation. Applied to a float dot-product
// intermediate produced by hoist_invariants(), it exposes an Int(32)
// accumulation (widening_mul-friendly) while every consumer keeps seeing the
// original Float(32) type via the inline cast-back wrapper.
int change_type_basic_test() {
    const int K = 64;
    ImageParam A{Int(8), 1, "A"}, B{Int(8), 1, "B"};
    ImageParam Scale{Float(32), 1, "Scale"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += Scale(i) * cast<float>(widening_mul(A(r), B(r)));

    Func Acc_wb = Acc.update().hoist_invariants();
    internal_assert(Acc_wb.types()[0] == Float(32))
        << "expected the hoisted intermediate to stay Float(32)\n";

    Func Acc_i32 = Acc_wb.change_type(Int(32));
    internal_assert(Acc_i32.types()[0] == Int(32))
        << "change_type: expected the retyped intermediate to be Int(32), got "
        << Acc_i32.types()[0] << "\n";
    // The original intermediate remains Float(32) for its consumers.
    internal_assert(Acc_wb.types()[0] == Float(32))
        << "change_type: the original Func must keep its type for consumers, got "
        << Acc_wb.types()[0] << "\n";
    Acc_i32.compute_root();

    const int M = 4;
    Buffer<int8_t> a(K), b(K);
    for (int k = 0; k < K; k++) {
        a(k) = (int8_t)((k % 15) - 7);
        b(k) = (int8_t)((k % 11) - 5);
    }
    Buffer<float> s(M);
    for (int m = 0; m < M; m++) {
        s(m) = 0.5f * (m + 1);
    }
    A.set(a);
    B.set(b);
    Scale.set(s);

    Buffer<float> result = Acc.realize({M});
    for (int m = 0; m < M; m++) {
        int32_t dot = 0;
        for (int k = 0; k < K; k++) {
            dot += (int32_t)a(k) * (int32_t)b(k);
        }
        float expected = s(m) * (float)dot;
        internal_assert(result(m) == expected)
            << "change_type basic mismatch at " << m << ": " << result(m)
            << " vs " << expected << "\n";
    }
    return 0;
}

// change_type() on a min reduction must use the reduction identity at the new
// type (Int(16).max()), not a lossy cast of the original Float(32) +inf.
int change_type_min_test() {
    ImageParam offset_p{Float(32), 1, "offset_p"};
    ImageParam data_p{Int(16), 2, "data_p"};

    Var i{"i"};
    const int K = 16;
    RDom k(0, K, "k");

    Func C{"C"};
    C(i) = Float(32).max();
    C(i) = min(C(i), offset_p(i) + data_p(i, k));

    Func C_wb = C.update().hoist_invariants();
    Func C_i16 = C_wb.change_type(Int(16));
    internal_assert(C_i16.types()[0] == Int(16))
        << "change_type min: expected Int(16), got " << C_i16.types()[0] << "\n";
    C_i16.compute_root();

    const int M = 8;
    Buffer<float> off(M);
    Buffer<int16_t> dat(M, K);
    for (int m = 0; m < M; m++) {
        off(m) = (float)(m + 1) * 0.5f;
        for (int kk = 0; kk < K; kk++) {
            dat(m, kk) = (int16_t)(((m * K + kk) % 21) - 10);
        }
    }
    offset_p.set(off);
    data_p.set(dat);

    Buffer<float> result = C.realize({M});
    for (int m = 0; m < M; m++) {
        float v = std::numeric_limits<float>::max();
        for (int kk = 0; kk < K; kk++) {
            v = std::min(v, off(m) + (float)dat(m, kk));
        }
        internal_assert(result(m) == v)
            << "change_type min mismatch at " << m << ": " << result(m) << " vs " << v << "\n";
    }
    return 0;
}

// change_type() must not fold a strict_float() through into a widening integer
// product: the reduction below rounds each term to float before summing, so the
// result must observe that rounding (giving exactly 0).
int change_type_strict_float_test() {
    Buffer<int32_t> data(2);
    data(0) = 16777217;  // not representable exactly as float32
    data(1) = -16777216;

    RDom k(0, 2, "k");

    Func f{"f"};
    f() = 0.0f;
    f() += 1.5f * strict_float(cast<float>(data(k)));

    Func intm = f.update().hoist_invariants();
    // Retype to Int(32): the strict_float call must survive (it is not an
    // implicit promotion cast), so each term is still rounded to float first.
    Func intm_i = intm.change_type(Int(32), /*unsafe=*/true);
    intm_i.compute_root();

    Buffer<float> result = f.realize();
    internal_assert(result() == 0.0f)
        << "change_type strict_float mismatch: " << result() << " vs 0\n";
    return 0;
}

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

#if HALIDE_WITH_EXCEPTIONS
// change_type() into a type too narrow to hold the accumulated sum must be
// rejected unless the unsafe flag is set.
int change_type_overflow_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    const int K = 256;
    ImageParam A{Int(8), 1, "A"}, B{Int(8), 1, "B"};
    Var i{"i"};
    RDom r(0, K, "r");

    auto build = [&]() {
        Func Acc{"Acc"};
        Acc(i) = 0.0f;
        Acc(i) += cast<float>(i) * cast<float>(widening_mul(A(r), B(r)));
        return Acc;
    };

    // 256 terms up to 127*127 ~= 4.1M overflows Int(16).
    bool error = false;
    try {
        Func Acc = build();
        Func wb = Acc.update().hoist_invariants();
        wb.change_type(Int(16));
    } catch (const Halide::CompileError &e) {
        error = true;
        printf("Expected overflow error:\n%s\n", e.what());
    }
    if (!error) {
        printf("change_type should have rejected the overflowing Int(16) accumulation!\n");
        return 1;
    }

    // With unsafe=true, the same call is allowed (the user takes responsibility).
    Func Acc = build();
    Func wb = Acc.update().hoist_invariants();
    Func typed = wb.change_type(Int(16), /*unsafe=*/true);
    internal_assert(typed.types()[0] == Int(16))
        << "change_type unsafe: expected Int(16), got " << typed.types()[0] << "\n";
    return 0;
}
#endif

}  // namespace

int main(int argc, char **argv) {
    printf("Running change_type_basic_test\n");
    if (change_type_basic_test()) {
        return 1;
    }
    printf("Running change_type_min_test\n");
    if (change_type_min_test()) {
        return 1;
    }
    printf("Running change_type_strict_float_test\n");
    if (change_type_strict_float_test()) {
        return 1;
    }
    printf("Running change_type_symbolic_extent_test\n");
    if (change_type_symbolic_extent_test()) {
        return 1;
    }
    printf("Running change_type_twice_test\n");
    if (change_type_twice_test()) {
        return 1;
    }
#if HALIDE_WITH_EXCEPTIONS
    printf("Running change_type_overflow_rejected_test\n");
    if (change_type_overflow_rejected_test()) {
        return 1;
    }
#endif

    printf("Success!\n");
    return 0;
}
