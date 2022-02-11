#include "Halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;
using namespace Halide::Internal;

Expr narrow(Expr a) {
    Type result_type = a.type().narrow();
    return Cast::make(result_type, std::move(a));
}

void check(Expr test, Expr expected, Type required_type) {
    // Some of the below tests assume the simplifier has run.
    test = simplify(test);

    // Make sure the pattern is robust to CSE. We only enforce this
    // for types with well-defined overflow for now.
    if (test.type().bits() < 32 || test.type().is_uint()) {
        auto bundle = [](const Expr &e) {
            return Call::make(e.type(), Call::bundle, {e, e}, Call::PureIntrinsic);
        };
        test = common_subexpression_elimination(bundle(test));
        expected = bundle(expected);
    }

    Expr result = substitute_in_all_lets(find_intrinsics(test));
    if (!equal(result, expected) || required_type != expected.type()) {
        std::cerr << "failure!\n";
        std::cerr << "test: " << test << "\n";
        std::cerr << "result: " << result << "\n";
        std::cerr << "expected: " << expected << "\n";
        abort();
    }
}

void check(Expr test, Expr expected) {
    return check(test, expected, expected.type());
}

template<typename T>
int64_t mul_shift_right(int64_t a, int64_t b, int q) {
    const int64_t min_t = std::numeric_limits<T>::min();
    const int64_t max_t = std::numeric_limits<T>::max();
    return std::min<int64_t>(std::max<int64_t>((a * b) >> q, min_t), max_t);
}

template<typename T>
int64_t rounding_mul_shift_right(int64_t a, int64_t b, int q) {
    const int64_t min_t = std::numeric_limits<T>::min();
    const int64_t max_t = std::numeric_limits<T>::max();
    return std::min<int64_t>(std::max<int64_t>((a * b + (1ll << (q - 1))) >> q, min_t), max_t);
}

template<typename T>
void check_intrinsics_over_range() {
    const int64_t min_t = std::numeric_limits<T>::min();
    const int64_t max_t = std::numeric_limits<T>::max();
    const int N = 64;
    Type halide_t = type_of<T>();

    const int t_bits = halide_t.bits();

    for (int i = 0; i < N; i++) {
        int64_t a = min_t + ((max_t - min_t) * i) / N;
        for (int j = 0; j < N; j++) {
            int64_t b = min_t + ((max_t - min_t) * j) / N;
            Expr a_expr = make_const(halide_t, a);
            Expr b_expr = make_const(halide_t, b);
            std::pair<Expr, int64_t> intrinsics_with_reference_answer[] = {
                {saturating_add(a_expr, b_expr), std::min(std::max(a + b, min_t), max_t)},
                {saturating_sub(a_expr, b_expr), std::min(std::max(a - b, min_t), max_t)},
                {halving_add(a_expr, b_expr), (a + b) >> 1},
                {rounding_halving_add(a_expr, b_expr), (a + b + 1) >> 1},
                {halving_sub(a_expr, b_expr), (a - b) >> 1},
                {rounding_halving_sub(a_expr, b_expr), (a - b + 1) >> 1},
            };
            for (const auto &p : intrinsics_with_reference_answer) {
                Expr test = lower_intrinsics(p.first);
                Expr result = simplify(test);
                if (!can_prove(result == make_const(halide_t, p.second))) {
                    std::cerr << "failure!\n";
                    std::cerr << "test: " << p.first << "\n";
                    std::cerr << "result: " << result << "\n";
                    std::cerr << "expected: " << p.second << "\n";
                    abort();
                }
            }

            std::pair<Expr, int64_t> multiply_intrinsics_with_reference_answer[] = {
                {mul_shift_right(a_expr, b_expr, t_bits - 1), mul_shift_right<T>(a, b, t_bits - 1)},
                {mul_shift_right(a_expr, b_expr, t_bits), mul_shift_right<T>(a, b, t_bits)},
                {rounding_mul_shift_right(a_expr, b_expr, t_bits - 1), rounding_mul_shift_right<T>(a, b, t_bits - 1)},
                {rounding_mul_shift_right(a_expr, b_expr, t_bits), rounding_mul_shift_right<T>(a, b, t_bits)},
            };
            for (const auto &p : multiply_intrinsics_with_reference_answer) {
                if (a < std::numeric_limits<int>::min() || a > std::numeric_limits<int>::max() ||
                    b < std::numeric_limits<int>::min() || b > std::numeric_limits<int>::max()) {
                    // Skip tests that would overflow the reference code.
                    continue;
                }
                Expr test = lower_intrinsics(p.first);
                Expr result = simplify(test);
                if (!can_prove(result == make_const(halide_t, p.second))) {
                    std::cerr << "failure!\n";
                    std::cerr << "test: " << p.first << "\n";
                    std::cerr << "result: " << result << "\n";
                    std::cerr << "expected: " << p.second << "\n";
                    abort();
                }
            }
        }
    }
}

Var x;
Expr make_leaf(Type t, const char *name) {
    return Load::make(t, name, Ramp::make(x, 1, t.lanes()),
                      Buffer<>{}, Parameter{},
                      const_true(t.lanes()), ModulusRemainder{});
}

int main(int argc, char **argv) {
    Expr i8x = make_leaf(Int(8, 4), "i8x");
    Expr i8y = make_leaf(Int(8, 4), "i8y");
    Expr i8z = make_leaf(Int(8, 4), "i8w");
    Expr i8w = make_leaf(Int(8, 4), "i8z");
    Expr u8x = make_leaf(UInt(8, 4), "u8x");
    Expr u8y = make_leaf(UInt(8, 4), "u8y");
    Expr u8z = make_leaf(UInt(8, 4), "u8w");
    Expr u8w = make_leaf(UInt(8, 4), "u8z");
    Expr u16x = make_leaf(UInt(16, 4), "u16x");
    Expr u32x = make_leaf(UInt(32, 4), "u32x");
    Expr u32y = make_leaf(UInt(32, 4), "u32y");
    Expr i32x = make_leaf(Int(32, 4), "i32x");
    Expr i32y = make_leaf(Int(32, 4), "i32y");
    Expr f16x = make_leaf(Float(16, 4), "f16x");
    Expr f16y = make_leaf(Float(16, 4), "f16y");
    Expr f32x = make_leaf(Float(32, 4), "f32x");
    Expr f32y = make_leaf(Float(32, 4), "f32y");

    // Check powers of two multiply/divide rewritten to shifts.
    check(i8x * 2, i8x << 1);
    check(u8x * 4, u8x << 2);
    check(i8x / 8, i8x >> 3);
    check(u8x / 4, u8x >> 2);

    check(i16(i8x) * 4096, widening_shift_left(i8x, 12));
    check(u16(u8x) * 128, widening_shift_left(u8x, 7));
    // check(u32(u8x) * 256, u32(widening_shift_left(u8x, u8(8))));

    // Check widening arithmetic
    check(i16(i8x) + i8y, widening_add(i8x, i8y));
    check(u16(u8x) + u8y, widening_add(u8x, u8y));
    check(i16(u8x) + u8y, i16(widening_add(u8x, u8y)));
    check(f32(f16x) + f32(f16y), widening_add(f16x, f16y));

    check(i16(i8x) - i8y, widening_sub(i8x, i8y));
    check(i16(u8x) - u8y, widening_sub(u8x, u8y));
    check(f32(f16x) - f32(f16y), widening_sub(f16x, f16y));

    check(i16(i8x) * i8y, widening_mul(i8x, i8y));
    check(u16(u8x) * u8y, widening_mul(u8x, u8y));
    check(i32(i8x) * i8y, i32(widening_mul(i8x, i8y)));
    check(u32(u8x) * u8y, u32(widening_mul(u8x, u8y)));
    check(f32(f16x) * f32(f16y), widening_mul(f16x, f16y));

    // Widening mul allows mixed signs
    check(i16(i8x) * u8y, widening_mul(i8x, u8y), Int(16, 4));
    check(i32(i8x) * u8y, i32(widening_mul(i8x, u8y)));

    // Excessive widening should be moved outside the arithmetic.
    check(widening_mul(i16(i8x), i16(i8y)), i32(widening_mul(i8x, i8y)));
    check(widening_mul(u16(u8x), u16(u8y)), u32(widening_mul(u8x, u8y)));
    check(widening_mul(i16(i8x), i16(u8y)), i32(widening_mul(i8x, u8y)));
    check(widening_mul(i16(u8x), i16(i8y)), i32(widening_mul(u8x, i8y)));
    check(widening_mul(f32(f16x), f32(f16y)), f64(widening_mul(f16x, f16y)));

    check(widening_add(i16(i8x), i16(i8y)), i32(widening_add(i8x, i8y)));
    check(widening_add(u16(u8x), u16(u8y)), u32(widening_add(u8x, u8y)));
    check(widening_add(i16(u8x), i16(u8y)), i32(widening_add(u8x, u8y)));
    check(widening_add(f32(f16x), f32(f16y)), f64(widening_add(f16x, f16y)));

    check(widening_mul(i32(i8x), i32(i8y)), i64(widening_mul(i8x, i8y)));
    check(widening_mul(u32(u8x), u32(u8y)), u64(widening_mul(u8x, u8y)));
    check(widening_mul(i32(i8x), i32(u8y)), i64(widening_mul(i8x, u8y)));
    check(widening_mul(i32(u8x), i32(i8y)), i64(widening_mul(u8x, i8y)));

    check(widening_add(i32(i8x), i32(i8y)), i64(widening_add(i8x, i8y)));
    check(widening_add(u32(u8x), u32(u8y)), u64(widening_add(u8x, u8y)));
    check(widening_add(i32(u8x), i32(u8y)), i64(widening_add(u8x, u8y)));

    // Tricky case.
    check(i32(u8x) + 1, i32(widening_add(u8x, u8(1))));

    // Check saturating arithmetic
    check(i8_sat(i16(i8x) + i8y), saturating_add(i8x, i8y));
    check(u8_sat(u16(u8x) + u8y), saturating_add(u8x, u8y));
    check(u8(min(u16(u8x) + u16(u8y), 255)), saturating_add(u8x, u8y));
    check(u8(min(i16(u8x) + i16(u8y), 255)), saturating_add(u8x, u8y));

    check(i8_sat(i16(i8x) - i8y), saturating_sub(i8x, i8y));
    check(u8(max(i16(u8x) - i16(u8y), 0)), saturating_sub(u8x, u8y));

    // Check halving arithmetic
    check(i8((i16(i8x) + i8y) / 2), halving_add(i8x, i8y));
    check(u8((u16(u8x) + u8y) / 2), halving_add(u8x, u8y));
    check(i8(widening_add(i8x, i8y) / 2), halving_add(i8x, i8y));
    check(u8(widening_add(u8x, u8y) / 2), halving_add(u8x, u8y));
    check((i32x + i32y) / 2, halving_add(i32x, i32y));
    check((f32x + f32y) / 2, (f32x + f32y) * 0.5f);

    check(i8((i16(i8x) - i8y) / 2), halving_sub(i8x, i8y));
    check(u8((u16(u8x) - u8y) / 2), halving_sub(u8x, u8y));
    check(i8(widening_sub(i8x, i8y) / 2), halving_sub(i8x, i8y));
    check(u8(widening_sub(u8x, u8y) / 2), halving_sub(u8x, u8y));
    check((i32x - i32y) / 2, halving_sub(i32x, i32y));
    check((f32x - f32y) / 2, (f32x - f32y) * 0.5f);

    check(i8((i16(i8x) + i8y + 1) / 2), rounding_halving_add(i8x, i8y));
    check(u8((u16(u8x) + u8y + 1) / 2), rounding_halving_add(u8x, u8y));
    check(i8((widening_add(i8x, i8y) + 1) / 2), rounding_halving_add(i8x, i8y));
    check(u8((widening_add(u8x, u8y) + 1) / 2), rounding_halving_add(u8x, u8y));
    check((i32x + i32y + 1) / 2, rounding_halving_add(i32x, i32y));

    check(i8((i16(i8x) - i8y + 1) / 2), rounding_halving_sub(i8x, i8y));
    check(u8((u16(u8x) - u8y + 1) / 2), rounding_halving_sub(u8x, u8y));
    check(i8((widening_sub(i8x, i8y) + 1) / 2), rounding_halving_sub(i8x, i8y));
    check(u8((widening_sub(u8x, u8y) + 1) / 2), rounding_halving_sub(u8x, u8y));
    check((i32x - i32y + 1) / 2, rounding_halving_sub(i32x, i32y));

    // Check absd
    check(abs(i16(i8x) - i16(i8y)), u16(absd(i8x, i8y)));
    check(abs(i16(u8x) - i16(u8y)), u16(absd(u8x, u8y)));
    check(abs(f16x - f16y), absd(f16x, f16y));
    check(abs(f32x - f32y), absd(f32x, f32y));
    check(abs(widening_sub(i8x, i8y)), u16(absd(i8x, i8y)));
    check(abs(widening_sub(u8x, u8y)), u16(absd(u8x, u8y)));
    check(abs(widening_sub(f16x, f16y)), f32(abs(f16x - f16y)));

    // Check rounding shifts
    // With constants
    check(narrow((i16(i8x) + 8) / 16), rounding_shift_right(i8x, 4));
    check(narrow(widening_add(i8x, i8(4)) / 8), rounding_shift_right(i8x, 3));
    check(i8(widening_add(i8x, i8(32)) / 64), rounding_shift_right(i8x, 6));
    check((i8x + i8(32)) / 64, (i8x + i8(32)) >> 6);  // Not a rounding_shift_right due to overflow.
    check((i32x + 16) / 32, rounding_shift_right(i32x, 5));

    // rounding_right_shift of a widening add can be strength-reduced
    check(narrow((u16(u8x) + 15) >> 4), rounding_halving_add(u8x, u8(14)) >> u8(3));
    check(narrow((u32(u16x) + 15) >> 4), rounding_halving_add(u16x, u16(14)) >> u16(3));

    // But not if the constant can't fit in the narrower type
    check(narrow((u16(u8x) + 500) >> 4), narrow((u16(u8x) + 500) >> 4));

    check((u64(u32x) + 8) / 16, u64(rounding_shift_right(u32x, 4)));
    check(u16(min((u64(u32x) + 8) / 16, 65535)), u16(min(rounding_shift_right(u32x, 4), 65535)));

    // And with variable shifts.
    check(i8(widening_add(i8x, (i8(1) << u8y) / 2) >> u8y), rounding_shift_right(i8x, u8y));
    check((i32x + (i32(1) << u32y) / 2) >> u32y, rounding_shift_right(i32x, u32y));

    check(i8(widening_add(i8x, (i8(1) << max(i8y, 0)) / 2) >> i8y), rounding_shift_right(i8x, i8y));
    check((i32x + (i32(1) << max(i32y, 0)) / 2) >> i32y, rounding_shift_right(i32x, i32y));

    check(i8(widening_add(i8x, (i8(1) >> min(i8y, 0)) / 2) << i8y), rounding_shift_left(i8x, i8y));
    check((i32x + (i32(1) >> min(i32y, 0)) / 2) << i32y, rounding_shift_left(i32x, i32y));

    check(i8(widening_add(i8x, (i8(1) << -min(i8y, 0)) / 2) << i8y), rounding_shift_left(i8x, i8y));
    check((i32x + (i32(1) << -min(i32y, 0)) / 2) << i32y, rounding_shift_left(i32x, i32y));
    check((i32x + (i32(1) << max(-i32y, 0)) / 2) << i32y, rounding_shift_left(i32x, i32y));

    // Test combinations of multiplies and adds with widening
    // Same sign:
    check(i16(i8x) * i8y + i16(i8z) * i8w, widening_mul(i8x, i8y) + widening_mul(i8z, i8w));
    check(u16(u8x) * u8y + u16(u8z) * u8w, widening_mul(u8x, u8y) + widening_mul(u8z, u8w));
    check(i32(i8x) * i8y + i32(i8z) * i8w, widening_add(widening_mul(i8x, i8y), widening_mul(i8z, i8w)));
    check(u32(u8x) * u8y + u32(u8z) * u8w, widening_add(widening_mul(u8x, u8y), widening_mul(u8z, u8w)), UInt(32, 4));

    // Mixed signs:
    check(i16(u8x) * i8y + i16(u8z) * i8w, widening_mul(u8x, i8y) + widening_mul(u8z, i8w), Int(16, 4));

    // Widening multiply-adds involving constants should be rewritten to adds whenever possible:
    check(i16(i8x) * 3 + i16(i8y) * 5, widening_mul(i8x, i8(3)) + widening_mul(i8y, i8(5)));
    check(i16(i8x) * 5 - i16(i8y) * 127, widening_mul(i8x, i8(5)) + widening_mul(i8y, i8(-127)));
    check(i16(u8x) * 3 - i16(u8y) * 7, widening_mul(u8x, i8(3)) + widening_mul(u8y, i8(-7)));
    check(i32(i8x) * 3 + i32(i8y) * 5, widening_add(widening_mul(i8x, i8(3)), widening_mul(i8y, i8(5))));
    check(i32(i8x) * 5 - i32(i8y) * 3, widening_add(widening_mul(i8x, i8(5)), widening_mul(i8y, i8(-3))));
    check(i32(u8x) * 7 - i32(u8y) * 6, widening_add(widening_mul(u8x, i8(7)), widening_mul(u8y, i8(-6))));

    check((i16(u8x) * 4 + i16(u8y)) * 3, widening_mul(u8x, i8(12)) + widening_mul(u8y, i8(3)));
    check((i16(i8y) * 7 + i16(i8x)) * 5, widening_mul(i8y, i8(35)) + widening_mul(i8x, i8(5)));
    check((i16(u8x) * 4 - i16(u8y)) * 3, widening_mul(u8x, i8(12)) + widening_mul(u8y, i8(-3)));
    check((i16(i8x) - i16(i8y) * 7) * 5, widening_mul(i8x, i8(5)) + widening_mul(i8y, i8(-35)));

    check((u16(u8x) + u16(u8y)) * 2, widening_shift_left(u8x, 1) + widening_shift_left(u8y, 1));
    check((u16(u8x) - u16(u8y)) * 2, widening_shift_left(u8x, 1) - widening_shift_left(u8y, 1));
    check((u16(u8x) * 4 + u16(u8y)) * 3, widening_mul(u8x, u8(12)) + widening_mul(u8y, u8(3)));
    check((u16(u8y) * 7 + u16(u8x)) * 5, widening_mul(u8y, u8(35)) + widening_mul(u8x, u8(5)));
    // TODO: Should these be rewritten to widening muls with mixed signs?
    check((u16(u8x) * 4 - u16(u8y)) * 3, widening_mul(u8x, u8(12)) - widening_mul(u8y, u8(3)));
    check((u16(u8x) - u16(u8y) * 7) * 5, widening_mul(u8x, u8(5)) - widening_mul(u8y, u8(35)));

    // Quantized multiplication.
    check(i8_sat(i16(i8x) * i16(i8y) >> 7), mul_shift_right(i8x, i8y, 7));
    check(i8(min(i16(i8x) * i16(i8y) >> 7, 127)), mul_shift_right(i8x, i8y, 7));
    check(i8_sat(i16(i8x) * i16(i8y) >> 8), mul_shift_right(i8x, i8y, 8));
    check(u8_sat(u16(u8x) * u16(u8y) >> 8), mul_shift_right(u8x, u8y, 8));
    check(i8(i16(i8x) * i16(i8y) >> 8), mul_shift_right(i8x, i8y, 8));
    check(u8(u16(u8x) * u16(u8y) >> 8), mul_shift_right(u8x, u8y, 8));

    // Multiplication of mixed-width integers
    check(u16(u32(u16x) * u32(u8y) >> 8), mul_shift_right(u16x, u16(u8y), 8));

    // Multiplication of mixed-sign integers shouldn't use mul_shift_right
    check(i32(i64(u32x) * i64(i32y) >> 32), i32(widening_mul(u32x, i32y) >> 32));

    check(i8_sat(rounding_shift_right(i16(i8x) * i16(i8y), 7)), rounding_mul_shift_right(i8x, i8y, 7));
    check(i8(min(rounding_shift_right(i16(i8x) * i16(i8y), 7), 127)), rounding_mul_shift_right(i8x, i8y, 7));
    check(i8_sat(rounding_shift_right(i16(i8x) * i16(i8y), 8)), rounding_mul_shift_right(i8x, i8y, 8));
    check(u8_sat(rounding_shift_right(u16(u8x) * u16(u8y), 8)), rounding_mul_shift_right(u8x, u8y, 8));
    check(i8(rounding_shift_right(i16(i8x) * i16(i8y), 8)), rounding_mul_shift_right(i8x, i8y, 8));
    check(u8(rounding_shift_right(u16(u8x) * u16(u8y), 8)), rounding_mul_shift_right(u8x, u8y, 8));

    check_intrinsics_over_range<int8_t>();
    check_intrinsics_over_range<uint8_t>();
    check_intrinsics_over_range<int16_t>();
    check_intrinsics_over_range<uint16_t>();
    check_intrinsics_over_range<int32_t>();
    check_intrinsics_over_range<uint32_t>();

    // The intrinsics-matching pass substitutes in widening lets. At
    // one point this caused a missing symbol bug for the code below
    // due to a subexpression not getting mutated.
    {
        Func f, g;
        Var x;
        Param<uint8_t> d;

        f(x) = cast<uint8_t>(x);
        f.compute_root();

        // We want a widening let that uses a load that uses a widening let

        // Widen it, but in a way that won't result in the cast being
        // substituted in by the simplifier. We want it to only be
        // substituted when we reach the intrinsics-matching pass.
        Expr widened = absd(cast<uint16_t>(f(x)), cast<uint16_t>(d));

        // Now use it in a load, twice, so that CSE pulls it out as a let.
        Expr lut = f(cast<int32_t>(widened * widened));

        // Now use that in another widening op...
        Expr widened2 = absd(cast<uint16_t>(lut), cast<uint16_t>(d));

        // ...which we will use twice so that CSE makes it another let.
        g(x) = widened2 * widened2;

        g.vectorize(x, 8);

        // This used to crash with a missing symbol error
        g.compile_jit();
    }

    printf("Success!\n");
    return 0;
}
