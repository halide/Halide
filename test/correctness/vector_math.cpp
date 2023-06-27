#include "Halide.h"
#include "test_sharding.h"

#include <algorithm>
#include <cmath>
#include <math.h>
#include <random>
#include <stdio.h>
#include <string.h>

using namespace Halide;

// Make some functions for turning types into strings
template<typename A>
const char *string_of_type();

#define DECL_SOT(name)                   \
    template<>                           \
    const char *string_of_type<name>() { \
        return #name;                    \
    }

DECL_SOT(uint8_t);
DECL_SOT(int8_t);
DECL_SOT(uint16_t);
DECL_SOT(int16_t);
DECL_SOT(uint32_t);
DECL_SOT(int32_t);
DECL_SOT(float);
DECL_SOT(double);
DECL_SOT(float16_t);
DECL_SOT(bfloat16_t);

template<typename A>
A mod(A x, A y);

template<>
float mod(float x, float y) {
    return fmod(x, y);
}

template<>
double mod(double x, double y) {
    return fmod(x, y);
}

template<>
float16_t mod(float16_t x, float16_t y) {
    return float16_t(fmod(float(x), float(y)));
}

template<>
bfloat16_t mod(bfloat16_t x, bfloat16_t y) {
    return bfloat16_t(fmod(float(x), float(y)));
}

template<typename A>
A mod(A x, A y) {
    return x % y;
}

template<typename A>
bool close_enough(A x, A y) {
    return x == y;
}

template<>
bool close_enough<float>(float x, float y) {
    return fabs(x - y) < 1e-4;
}

template<>
bool close_enough<double>(double x, double y) {
    return fabs(x - y) < 1e-5;
}

template<>
bool close_enough<float16_t>(float16_t x, float16_t y) {
    if (x == y) return true;
    float16_t upper = float16_t::make_from_bits(x.to_bits() + 2);
    float16_t lower = float16_t::make_from_bits(x.to_bits() - 2);
    if (lower > upper) std::swap(lower, upper);
    return y >= lower && y <= upper;
}

template<>
bool close_enough<bfloat16_t>(bfloat16_t x, bfloat16_t y) {
    if (x == y) return true;
    bfloat16_t upper = bfloat16_t::make_from_bits(x.to_bits() + 2);
    bfloat16_t lower = bfloat16_t::make_from_bits(x.to_bits() - 2);
    if (lower > upper) std::swap(lower, upper);
    return (y >= lower) && (y <= upper);
}

template<typename T>
T divide(T x, T y) {
    return (x - (((x % y) + y) % y)) / y;
}

template<>
float divide(float x, float y) {
    return x / y;
}

template<>
double divide(double x, double y) {
    return x / y;
}

template<>
float16_t divide(float16_t x, float16_t y) {
    return x / y;
}

template<>
bfloat16_t divide(bfloat16_t x, bfloat16_t y) {
    return x / y;
}

template<typename A>
A absd(A x, A y) {
    return x > y ? x - y : y - x;
}

int mantissa(float x) {
    int bits = 0;
    memcpy(&bits, &x, 4);
    return bits & 0x007fffff;
}

template<typename T>
struct with_unsigned {
    typedef T type;
};

template<>
struct with_unsigned<int8_t> {
    typedef uint8_t type;
};

template<>
struct with_unsigned<int16_t> {
    typedef uint16_t type;
};

template<>
struct with_unsigned<int32_t> {
    typedef uint32_t type;
};

template<>
struct with_unsigned<int64_t> {
    typedef uint64_t type;
};

template<typename A>
bool test(int lanes, int seed) {
    const int W = 320;
    const int H = 16;

    const int verbose = false;

    printf("Testing %sx%d\n", string_of_type<A>(), lanes);

    // use std::mt19937 instead of rand() to ensure consistent behavior on all systems
    std::mt19937 rng(seed);
    std::uniform_int_distribution<> dis(0, 1023);

    Buffer<A> input(W + 16, H + 16);
    for (int y = 0; y < H + 16; y++) {
        for (int x = 0; x < W + 16; x++) {
            // We must ensure that the result of casting is not out-of-range:
            // float->int casts are UB if the result doesn't fit.
            input(x, y) = (A)(dis(rng) * 0.0625 + 1.0);
            if ((A)(-1) < (A)(0)) {
                input(x, y) -= (A)(10);
            }
        }
    }
    Var x, y;

    // Add
    {
        if (verbose) printf("Add\n");
        Func f1;
        f1(x, y) = input(x, y) + input(x + 1, y);
        f1.vectorize(x, lanes);
        Buffer<A> im1 = f1.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = input(x, y) + input(x + 1, y);
                if (im1(x, y) != correct) {
                    printf("im1(%d, %d) = %f instead of %f\n", x, y, (double)(im1(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // Sub
    {
        if (verbose) printf("Subtract\n");
        Func f2;
        f2(x, y) = input(x, y) - input(x + 1, y);
        f2.vectorize(x, lanes);
        Buffer<A> im2 = f2.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = input(x, y) - input(x + 1, y);
                if (im2(x, y) != correct) {
                    printf("im2(%d, %d) = %f instead of %f\n", x, y, (double)(im2(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // Mul
    {
        if (verbose) printf("Multiply\n");
        Func f3;
        f3(x, y) = input(x, y) * input(x + 1, y);
        f3.vectorize(x, lanes);
        Buffer<A> im3 = f3.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = input(x, y) * input(x + 1, y);
                if (im3(x, y) != correct) {
                    printf("im3(%d, %d) = %f instead of %f\n", x, y, (double)(im3(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // select
    {
        if (verbose) printf("Select\n");
        Func f4;
        f4(x, y) = select(input(x, y) > input(x + 1, y), input(x + 2, y), input(x + 3, y));
        f4.vectorize(x, lanes);
        Buffer<A> im4 = f4.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = input(x, y) > input(x + 1, y) ? input(x + 2, y) : input(x + 3, y);
                if (im4(x, y) != correct) {
                    printf("im4(%d, %d) = %f instead of %f\n", x, y, (double)(im4(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // Gather
    {
        if (verbose) printf("Gather\n");
        Func f5;
        Expr xCoord = clamp(cast<int>(input(x, y)), 0, W - 1);
        Expr yCoord = clamp(cast<int>(input(x + 1, y)), 0, H - 1);
        f5(x, y) = input(xCoord, yCoord);
        f5.vectorize(x, lanes);
        Buffer<A> im5 = f5.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int xCoord = (int)(input(x, y));
                if (xCoord >= W) xCoord = W - 1;
                if (xCoord < 0) xCoord = 0;

                int yCoord = (int)(input(x + 1, y));
                if (yCoord >= H) yCoord = H - 1;
                if (yCoord < 0) yCoord = 0;

                A correct = input(xCoord, yCoord);

                if (im5(x, y) != correct) {
                    printf("im5(%d, %d) = %f instead of %f\n", x, y, (double)(im5(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // Gather and scatter with constant but unknown stride
    {
        Func f5a;
        f5a(x, y) = input(x, y) * cast<A>(2);
        f5a.vectorize(y, lanes);
        Buffer<A> im5a = f5a.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = input(x, y) * ((A)(2));
                if (im5a(x, y) != correct) {
                    printf("im5a(%d, %d) = %f instead of %f\n", x, y, (double)(im5a(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // Scatter
    {
        if (verbose) printf("Scatter\n");
        Func f6;
        // Set one entry in each column high
        f6(x, y) = 0;
        f6(x, clamp(x * x, 0, H - 1)) = 1;

        f6.update().vectorize(x, lanes);

        Buffer<int> im6 = f6.realize({W, H});

        for (int x = 0; x < W; x++) {
            int yCoord = x * x;
            if (yCoord >= H) yCoord = H - 1;
            if (yCoord < 0) yCoord = 0;
            for (int y = 0; y < H; y++) {
                int correct = y == yCoord ? 1 : 0;
                if (im6(x, y) != correct) {
                    printf("im6(%d, %d) = %d instead of %d\n", x, y, im6(x, y), correct);
                    return false;
                }
            }
        }
    }

    // Min/max
    {
        if (verbose) printf("Min/max\n");
        Func f7;
        f7(x, y) = clamp(input(x, y), cast<A>(10), cast<A>(20));
        f7.vectorize(x, lanes);
        Buffer<A> im7 = f7.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                if (im7(x, y) < (A)10 || im7(x, y) > (A)20) {
                    printf("im7(%d, %d) = %f\n", x, y, (double)(im7(x, y)));
                    return false;
                }
            }
        }
    }

    // Extern function call
    {
        if (verbose) printf("External call to hypot\n");
        Func f8;
        f8(x, y) = hypot(1.1f, cast<float>(input(x, y)));
        f8.vectorize(x, lanes);
        Buffer<float> im8 = f8.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float correct = hypotf(1.1f, (float)input(x, y));
                if (!close_enough(im8(x, y), correct)) {
                    printf("im8(%d, %d) = %f instead of %f\n",
                           x, y, (double)im8(x, y), correct);
                    return false;
                }
            }
        }
    }

    // Div
    {
        if (verbose) printf("Division\n");
        Func f9;
        f9(x, y) = input(x, y) / clamp(input(x + 1, y), cast<A>(1), cast<A>(3));
        f9.vectorize(x, lanes);
        Buffer<A> im9 = f9.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A clamped = input(x + 1, y);
                if (clamped < (A)1) clamped = (A)1;
                if (clamped > (A)3) clamped = (A)3;
                A correct = divide(input(x, y), clamped);
                // We allow floating point division to take some liberties with accuracy
                if (!close_enough(im9(x, y), correct)) {
                    printf("im9(%d, %d) = %f/%f = %f instead of %f\n",
                           x, y,
                           (double)input(x, y), (double)clamped,
                           (double)(im9(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // Divide by small constants
    {
        if (verbose) printf("Dividing by small constants\n");
        for (int c = 2; c < 16; c++) {
            Func f10;
            f10(x, y) = (input(x, y)) / cast<A>(Expr(c));
            f10.vectorize(x, lanes);
            Buffer<A> im10 = f10.realize({W, H});

            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    A correct = divide(input(x, y), (A)c);

                    if (!close_enough(im10(x, y), correct)) {
                        printf("im10(%d, %d) = %f/%d = %f instead of %f\n", x, y,
                               (double)(input(x, y)), c,
                               (double)(im10(x, y)),
                               (double)(correct));
                        printf("Error when dividing by %d\n", c);
                        return false;
                    }
                }
            }
        }
    }

    // Interleave
    {
        if (verbose) printf("Interleaving store\n");
        Func f11;
        f11(x, y) = select((x % 2) == 0, input(x / 2, y), input(x / 2, y + 1));
        f11.vectorize(x, lanes);
        Buffer<A> im11 = f11.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = ((x % 2) == 0) ? input(x / 2, y) : input(x / 2, y + 1);
                if (im11(x, y) != correct) {
                    printf("im11(%d, %d) = %f instead of %f\n", x, y, (double)(im11(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // Reverse
    {
        if (verbose) printf("Reversing\n");
        Func f12;
        f12(x, y) = input(W - 1 - x, H - 1 - y);
        f12.vectorize(x, lanes);
        Buffer<A> im12 = f12.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = input(W - 1 - x, H - 1 - y);
                if (im12(x, y) != correct) {
                    printf("im12(%d, %d) = %f instead of %f\n", x, y, (double)(im12(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // Unaligned load with known shift
    {
        if (verbose) printf("Unaligned load\n");
        Func f13;
        f13(x, y) = input(x + 3, y);
        f13.vectorize(x, lanes);
        Buffer<A> im13 = f13.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = input(x + 3, y);
                if (im13(x, y) != correct) {
                    printf("im13(%d, %d) = %f instead of %f\n", x, y, (double)(im13(x, y)), (double)(correct));
                }
            }
        }
    }

    // Absolute value
    {
        if (!type_of<A>().is_uint()) {
            if (verbose) printf("Absolute value\n");
            Func f14;
            f14(x, y) = cast<A>(abs(input(x, y)));
            Buffer<A> im14 = f14.realize({W, H});

            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    A correct = input(x, y);
                    if (correct <= A(0)) correct = -correct;
                    if (im14(x, y) != correct) {
                        printf("im14(%d, %d) = %f instead of %f\n", x, y, (double)(im14(x, y)), (double)(correct));
                    }
                }
            }
        }
    }

    // pmaddwd
    {
        if (type_of<A>() == Int(16)) {
            if (verbose) printf("pmaddwd\n");
            Func f15, f16;
            f15(x, y) = cast<int>(input(x, y)) * input(x, y + 2) + cast<int>(input(x, y + 1)) * input(x, y + 3);
            f16(x, y) = cast<int>(input(x, y)) * input(x, y + 2) - cast<int>(input(x, y + 1)) * input(x, y + 3);
            f15.vectorize(x, lanes);
            f16.vectorize(x, lanes);
            Buffer<int32_t> im15 = f15.realize({W, H});
            Buffer<int32_t> im16 = f16.realize({W, H});
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    int correct15 = int(input(x, y) * input(x, y + 2) + input(x, y + 1) * input(x, y + 3));
                    int correct16 = int(input(x, y) * input(x, y + 2) - input(x, y + 1) * input(x, y + 3));
                    if (im15(x, y) != correct15) {
                        printf("im15(%d, %d) = %d instead of %d\n", x, y, im15(x, y), correct15);
                    }
                    if (im16(x, y) != correct16) {
                        printf("im16(%d, %d) = %d instead of %d\n", x, y, im16(x, y), correct16);
                    }
                }
            }
        }
    }

    // Fast exp, log, and pow
    if (type_of<A>() == Float(32)) {
        if (verbose) printf("Fast transcendentals\n");
        Buffer<float> im15, im16, im17, im18, im19, im20;
        Expr a = input(x, y) * 0.5f;
        Expr b = input((x + 1) % W, y) * 0.5f;
        {
            Func f15;
            f15(x, y) = log(a);
            im15 = f15.realize({W, H});
        }
        {
            Func f16;
            f16(x, y) = exp(b);
            im16 = f16.realize({W, H});
        }
        {
            Func f17;
            f17(x, y) = pow(a, b / 16.0f);
            im17 = f17.realize({W, H});
        }
        {
            Func f18;
            f18(x, y) = fast_log(a);
            im18 = f18.realize({W, H});
        }
        {
            Func f19;
            f19(x, y) = fast_exp(b);
            im19 = f19.realize({W, H});
        }
        {
            Func f20;
            f20(x, y) = fast_pow(a, b / 16.0f);
            im20 = f20.realize({W, H});
        }

        int worst_log_mantissa = 0;
        int worst_exp_mantissa = 0;
        int worst_pow_mantissa = 0;
        int worst_fast_log_mantissa = 0;
        int worst_fast_exp_mantissa = 0;
        int worst_fast_pow_mantissa = 0;

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float a = float(input(x, y)) * 0.5f;
                float b = float(input((x + 1) % W, y)) * 0.5f;
                float correct_log = logf(a);
                float correct_exp = expf(b);
                float correct_pow = powf(a, b / 16.0f);

                int correct_log_mantissa = mantissa(correct_log);
                int correct_exp_mantissa = mantissa(correct_exp);
                int correct_pow_mantissa = mantissa(correct_pow);

                int log_mantissa = mantissa(im15(x, y));
                int exp_mantissa = mantissa(im16(x, y));
                int pow_mantissa = mantissa(im17(x, y));

                int fast_log_mantissa = mantissa(im18(x, y));
                int fast_exp_mantissa = mantissa(im19(x, y));
                int fast_pow_mantissa = mantissa(im20(x, y));

                int log_mantissa_error = abs(log_mantissa - correct_log_mantissa);
                int exp_mantissa_error = abs(exp_mantissa - correct_exp_mantissa);
                int pow_mantissa_error = abs(pow_mantissa - correct_pow_mantissa);
                int fast_log_mantissa_error = abs(fast_log_mantissa - correct_log_mantissa);
                int fast_exp_mantissa_error = abs(fast_exp_mantissa - correct_exp_mantissa);
                int fast_pow_mantissa_error = abs(fast_pow_mantissa - correct_pow_mantissa);

                worst_log_mantissa = std::max(worst_log_mantissa, log_mantissa_error);
                worst_exp_mantissa = std::max(worst_exp_mantissa, exp_mantissa_error);

                if (a >= 0) {
                    worst_pow_mantissa = std::max(worst_pow_mantissa, pow_mantissa_error);
                }

                if (std::isfinite(correct_log)) {
                    worst_fast_log_mantissa = std::max(worst_fast_log_mantissa, fast_log_mantissa_error);
                }

                if (std::isfinite(correct_exp)) {
                    worst_fast_exp_mantissa = std::max(worst_fast_exp_mantissa, fast_exp_mantissa_error);
                }

                if (std::isfinite(correct_pow) && a > 0) {
                    worst_fast_pow_mantissa = std::max(worst_fast_pow_mantissa, fast_pow_mantissa_error);
                }

                if (log_mantissa_error > 8) {
                    printf("log(%f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           a, im15(x, y), correct_log, correct_log_mantissa, log_mantissa);
                }
                if (exp_mantissa_error > 32) {
                    // Actually good to the last 2 bits of the mantissa with sse4.1 / avx
                    printf("exp(%f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           b, im16(x, y), correct_exp, correct_exp_mantissa, exp_mantissa);
                }
                if (a >= 0 && pow_mantissa_error > 64) {
                    printf("pow(%f, %f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           a, b / 16.0f, im17(x, y), correct_pow, correct_pow_mantissa, pow_mantissa);
                }
                if (std::isfinite(correct_log) && fast_log_mantissa_error > 64) {
                    printf("fast_log(%f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           a, im18(x, y), correct_log, correct_log_mantissa, fast_log_mantissa);
                }
                if (std::isfinite(correct_exp) && fast_exp_mantissa_error > 64) {
                    printf("fast_exp(%f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           b, im19(x, y), correct_exp, correct_exp_mantissa, fast_exp_mantissa);
                }
                if (a >= 0 && std::isfinite(correct_pow) && fast_pow_mantissa_error > 128) {
                    printf("fast_pow(%f, %f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           a, b / 16.0f, im20(x, y), correct_pow, correct_pow_mantissa, fast_pow_mantissa);
                }
            }
        }

        /*
        printf("log mantissa error: %d\n", worst_log_mantissa);
        printf("exp mantissa error: %d\n", worst_exp_mantissa);
        printf("pow mantissa error: %d\n", worst_pow_mantissa);
        printf("fast_log mantissa error: %d\n", worst_fast_log_mantissa);
        printf("fast_exp mantissa error: %d\n", worst_fast_exp_mantissa);
        printf("fast_pow mantissa error: %d\n", worst_fast_pow_mantissa);
        */
    }

    // Lerp (where the weight is the same type as the values)
    {
        if (verbose) printf("Lerp\n");
        Func f21;
        Expr weight = input(x + 2, y);
        Type t = type_of<A>();
        if (t.is_float()) {
            weight = clamp(weight, cast<A>(0), cast<A>(1));
        } else if (t.is_int()) {
            weight = cast(UInt(t.bits(), t.lanes()), max(0, weight));
        }
        f21(x, y) = lerp(input(x, y), input(x + 1, y), weight);
        Buffer<A> im21 = f21.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                double a = (double)(input(x, y));
                double b = (double)(input(x + 1, y));
                double w = (double)(input(x + 2, y));
                if (w < 0) w = 0;
                if (!t.is_float()) {
                    uint64_t divisor = 1;
                    divisor <<= t.bits();
                    divisor -= 1;
                    w /= divisor;
                }
                w = std::min(std::max(w, 0.0), 1.0);

                double lerped = (a * (1.0 - w) + b * w);
                if (!t.is_float()) {
                    lerped = floor(lerped + 0.5);
                }
                A correct = (A)(lerped);
                if (im21(x, y) != correct) {
                    printf("lerp(%f, %f, %f) = %f instead of %f\n", a, b, w, (double)(im21(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    // Absolute difference
    {
        if (verbose) printf("Absolute difference\n");
        Func f22;
        f22(x, y) = absd(input(x, y), input(x + 1, y));
        f22.vectorize(x, lanes);
        Buffer<typename with_unsigned<A>::type> im22 = f22.realize({W, H});

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                using T = typename with_unsigned<A>::type;
                T correct = T(absd((double)input(x, y), (double)input(x + 1, y)));
                if (im22(x, y) != correct) {
                    printf("im22(%d, %d) = %f instead of %f\n", x, y, (double)(im22(x, y)), (double)(correct));
                    return false;
                }
            }
        }
    }

    return true;
}

int main(int argc, char **argv) {
    int seed = argc > 1 ? atoi(argv[1]) : time(nullptr);
    std::cout << "vector_math test seed: " << seed << std::endl;

    struct Task {
        std::function<bool(int, int)> fn;
        int lanes;
        int seed;
    };

    // Only native vector widths - llvm doesn't handle others well
    std::vector<Task> tasks = {
        {test<float>, 4, seed},
        {test<float>, 8, seed},
        {test<double>, 2, seed},
        {test<uint8_t>, 16, seed},
        {test<int8_t>, 16, seed},
        {test<uint16_t>, 8, seed},
        {test<int16_t>, 8, seed},
        {test<uint32_t>, 4, seed},
        {test<int32_t>, 4, seed},
        {test<bfloat16_t>, 8, seed},
        {test<bfloat16_t>, 16, seed},
        {test<float16_t>, 8, seed},
        {test<float16_t>, 16, seed},
    };

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        if (!task.fn(task.lanes, task.seed)) {
            exit(1);
        }
    }

    printf("Success!\n");
    return 0;
}
