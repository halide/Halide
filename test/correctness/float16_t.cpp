#include "Halide.h"

#include <limits>

#if defined(__linux__) && defined(__clang__)
// If LLVM was built with an older GCC but Halide is built with Clang,
// we may be missing this symbol needed for float16 conversion.
// Just insert a weak definition here as a workaround.
extern "C" {

#if __clang_major__ >= 15 && defined(__x86_64__)

// In Clang 15 and later, this function is passed a uint16... but in the xmm0 register on x86-64.
// So we'll declare it as a float and just grab the upper 16 bits.
__attribute__((weak)) float __extendhfsf2(float actually_a_float16) {
    uint16_t data;
    memcpy(&data, &actually_a_float16, sizeof(data));
    return (float)Halide::float16_t::make_from_bits(data);
}

#else

__attribute__((weak)) float __extendhfsf2(uint16_t data) {
    return (float)Halide::float16_t::make_from_bits(data);
}

#endif

}  // extern "C"
#endif

namespace {

using namespace Halide;

bool check_infinity_case(bool use_first, float16_t value, const char *value_name,
                         int increment, float16_t expected_first, float16_t expected_second,
                         const char *first_name, const char *second_name) {
    if (value != (use_first ? expected_first : expected_second)) {
        printf("%s %d is %x, not %s.\n", value_name, increment, value.to_bits(),
               (use_first ? first_name : second_name));
        return false;
    }
    return true;
}

class MyCustomErrorReporter : public CompileTimeErrorReporter {
public:
    MyCustomErrorReporter() = default;

    void warning(const char *msg) override {
        // Just ignore them, they are probably warnings about emulated float16, which we don't care about here
    }

    void error(const char *msg) override {
        fprintf(stderr, "Error: %s\n", msg);
        exit(1);
    }
};

template<typename FP16>
int run_test() {
    Var x;

    Buffer<float16_t> in1 = lambda(x, cast<float16_t>(-0.5f) + cast<float16_t>(x) / (128)).realize({128});
    Buffer<bfloat16_t> in2 = lambda(x, cast<bfloat16_t>(-0.5f) + cast<bfloat16_t>(x) / (128)).realize({128});

    // Check the Halide-side float 16 conversion math matches the C++-side math.
    in1.for_each_element([&](int i) {
        float16_t correct = Halide::float16_t(-0.5f) + Halide::float16_t(i) / Halide::float16_t(128.0f);
        if (in1(i) != correct) {
            fprintf(stderr, "in1(%d) = %f instead of %f\n", i, float(in2(i)), float(correct));
            exit(1);
        }
    });

    in2.for_each_element([&](int i) {
        bfloat16_t correct = Halide::bfloat16_t(-0.5f) + Halide::bfloat16_t(i) / Halide::bfloat16_t(128.0f);
        if (in2(i) != correct) {
            fprintf(stderr, "in2(%d) = %f instead of %f\n", i, float(in2(i)), float(correct));
            exit(1);
        }
    });

    // Check some basic math works on float16. More math is tested in
    // correctness_vector_math.
    Func wrap1, wrap2;
    wrap1(x) = in1(x);
    wrap2(x) = in2(x);

    Func f;
    f(x) = abs(sqrt(abs(wrap1(x) * 4.0f)) - sqrt(abs(wrap2(x))) * 2.0f);

    f.compute_root().vectorize(x, 16);
    wrap1.compute_at(f, x).vectorize(x);
    wrap2.compute_at(f, x).vectorize(x);

    RDom r(0, 128);
    Func g;
    g() = maximum(cast<double>(f(r)));

    double d = evaluate<double>(g());
    if (d != 0) {
        fprintf(stderr, "Should be zero: %f\n", d);
        return 1;
    }

    // Check scalar parameters
    {
        Param<float16_t> a;
        Param<bfloat16_t> b;
        a.set(float16_t(1.5f));
        b.set(bfloat16_t(2.75f));
        float result = evaluate<float>(cast<float>(a) + cast<float>(b));
        if (result != 4.25f) {
            fprintf(stderr, "Incorrect result: %f != 4.25f\n", result);
            return 1;
        }
    }

    // Check scalar parameters work using a problematic case
    {
        Param<float16_t> a, b, c;
        a.set(float16_t(24.062500f));
        b.set(float16_t(30.187500f));
        c.set(float16_t(0));
        float16_t result = evaluate<float16_t>(lerp(a, b, c));
        if (float(result) != 24.062500f) {
            fprintf(stderr, "Incorrect result: %f != 24.0625f\n", (float)result);
            return 1;
        }
    }

    {
        Param<bfloat16_t> a, b, c;
        a.set(bfloat16_t(24.5f));
        b.set(bfloat16_t(30.5f));
        c.set(bfloat16_t(0));
        bfloat16_t result = evaluate<bfloat16_t>(lerp(a, b, c));
        if (float(result) != 24.5f) {
            fprintf(stderr, "Incorrect result: %f != 24.5f\n", (float)result);
            return 1;
        }
    }

    // Check that ties round towards a zero last bit on narrowing conversions
    {
        bfloat16_t start = bfloat16_t(37.2789f);
        for (uint16_t x = 0; x < 8; x++) {
            bfloat16_t a = bfloat16_t::make_from_bits(start.to_bits() + x);
            bfloat16_t b = bfloat16_t::make_from_bits(start.to_bits() + x + 1);
            bfloat16_t ab = bfloat16_t(((float)a + (float)b) / 2);

            if (a > ab || ab > b) {
                fprintf(stderr, "Misordered: %x %x %x\n", a.to_bits(), ab.to_bits(), b.to_bits());
            }

            bool ok = (((a.to_bits() & 1) && (ab == b)) ||
                       ((b.to_bits() & 1) && (ab == a)));

            if (!ok) {
                fprintf(stderr, "Incorrect rounding: %x %x %x\n", a.to_bits(), ab.to_bits(), b.to_bits());
                return 1;
            }
        }
    }

    // Check that ties round towards a zero last bit on narrowing conversions
    {
        float16_t start = float16_t(37.2789f);
        for (uint16_t x = 0; x < 8; x++) {
            float16_t a = float16_t::make_from_bits(start.to_bits() + x);
            float16_t b = float16_t::make_from_bits(start.to_bits() + x + 1);
            float16_t ab = float16_t(((float)a + (float)b) / 2);

            if (a > ab || ab > b) {
                fprintf(stderr, "Misordered: %x %x %x\n", a.to_bits(), ab.to_bits(), b.to_bits());
            }

            bool ok = (((a.to_bits() & 1) && (ab == b)) ||
                       ((b.to_bits() & 1) && (ab == a)));

            if (!ok) {
                fprintf(stderr, "Incorrect rounding: %x %x %x\n", a.to_bits(), ab.to_bits(), b.to_bits());
                return 1;
            }
        }
    }

    // Check rounding intrinsics
    {
        Func noise;
        Var x;
        noise(x) = (random_int() % 256) * 0.1f;
        noise.compute_root();
        Func trunc_f32 = lambda(x, trunc(noise(x)));
        Func round_f32 = lambda(x, round(noise(x)));
        Func ceil_f32 = lambda(x, ceil(noise(x)));
        Func floor_f32 = lambda(x, floor(noise(x)));
        Func trunc_f16 = lambda(x, trunc(cast<float16_t>(noise(x))));
        Func round_f16 = lambda(x, round(cast<float16_t>(noise(x))));
        Func ceil_f16 = lambda(x, ceil(cast<float16_t>(noise(x))));
        Func floor_f16 = lambda(x, floor(cast<float16_t>(noise(x))));

        std::vector<Func> funcs = {trunc_f32, round_f32, ceil_f32, floor_f32,
                                   trunc_f16, round_f16, ceil_f16, floor_f16};

        for (auto f : funcs) {
            f.compute_root().vectorize(x, 16);
        }

        const char *names[] = {"trunc", "round", "ceil", "floor"};

        Pipeline p(funcs);
        Realization r = p.realize({1024});
        for (int i = 0; i < 1024; i++) {
            for (int j = 0; j < 4; j++) {
                float f32 = Buffer<float>(r[j])(i);
                float f16 = float(Buffer<float16_t>(r[j + 4])(i));
                if (f32 != f16) {
                    fprintf(stderr, "%s outputs do not match: %f %f\n",
                            names[j], f32, f16);
                    return 1;
                }
            }
        }
    }

    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::CUDA) ||
        target.has_feature(Target::Metal)) {
        // Check we can pass a float16 to a GPU kernel. Skip OpenCL
        // because support is spotty.
        Var x, y;
        ImageParam input(Float(16), 2);
        Param<float16_t> mul("mul");

        Func output;
        output(x, y) = x * y * (input(x, y) * mul);

        Var xi, yi;
        output.gpu_tile(x, y, xi, yi, 8, 8);

        mul.set(float16_t(2.0f));
        Buffer<float16_t> in(8, 8);
        in.fill(float16_t(0.25f));
        input.set(in);
        Buffer<float16_t> buf = output.realize({8, 8});
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                float16_t correct = float16_t((x * y) / 2.0f);
                if (buf(x, y).to_bits() != correct.to_bits()) {
                    fprintf(stderr, "buf(%d, %d) = 0x%x instead of 0x%x\n",
                            x, y, buf(x, y).to_bits(), correct.to_bits());
                    return 1;
                }
            }
        }
    }

    {
        // Check constants are emitted correctly
        Func out;
        float16_t constant(100.0f);
        out() = constant;
        Buffer<float16_t> buf = out.realize();
        if (buf(0) != constant) {
            fprintf(stderr, "buf(0) = %f instead of %f\n", float(buf(0)), float(constant));
            return 1;
        }
    }

    // Enable to read assembly generated by the conversion routines
    if ((false)) {  // Intentional dead code. Extra parens to pacify clang-tidy.
        Func src, to_f16, from_f16;

        src(x) = cast<float>(x);
        to_f16(x) = cast<float16_t>(src(x));
        from_f16(x) = cast<float>(to_f16(x));

        src.compute_root().vectorize(x, 8, TailStrategy::RoundUp);
        to_f16.compute_root().vectorize(x, 8, TailStrategy::RoundUp);
        from_f16.compute_root().vectorize(x, 8, TailStrategy::RoundUp);

        from_f16.compile_to_assembly("/dev/stdout", {}, Target("host-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_unroll-disable_llvm_loop_vectorize"));
    }

    // Check infinity handling for both float16_t and Halide codegen.
    {
        std::pair<int, bool> test_cases[] =
            {{1, false}, {16, true}, {256, true}};

        for (const auto &test_case : test_cases) {
            float16_t max_pos_val = float16_t::make_from_bits(0x7bff);
            float16_t min_neg_val = float16_t::make_from_bits(0xfbff);
            float16_t increment(test_case.first);

            float16_t max_plus_increment(max_pos_val + increment);
            if (!check_infinity_case(test_case.second, max_plus_increment,
                                     "float16_t maximum value plus", test_case.first,
                                     float16_t::make_infinity(), max_pos_val,
                                     "positive infinity", "maximum positive value")) {
                return 1;
            }

            float16_t min_minus_increment(min_neg_val - increment);
            if (!check_infinity_case(test_case.second, min_minus_increment,
                                     "float16_t minimum value minus", test_case.first,
                                     float16_t::make_negative_infinity(), min_neg_val,
                                     "negative infinity", "maximum negative value")) {
                return 1;
            }

            Param<float16_t> a("a"), b("b");
            a.set(max_pos_val);
            b.set(increment);
            float16_t c = evaluate<float16_t>(a + b);
            if (!check_infinity_case(test_case.second, c,
                                     "Halide float16_t maximum value plus", test_case.first,
                                     float16_t::make_infinity(), max_pos_val,
                                     "positive infinity", "maximum positive value")) {
                return 1;
            }

            a.set(min_neg_val);
            c = evaluate<float16_t>(a - b);
            if (!check_infinity_case(test_case.second, c,
                                     "Halide float16_t minimum value minus", test_case.first,
                                     float16_t::make_negative_infinity(), min_neg_val,
                                     "negative infinity", "maximum negative value")) {
                return 1;
            }

            float pos_inf = std::numeric_limits<float>::infinity();
            float16_t fp16_pos_inf(pos_inf);
            if (fp16_pos_inf != float16_t::make_infinity()) {
                fprintf(stderr, "Conversion of 32-bit positive infinity to 16-bit float is %x, not positive infinity.\n", fp16_pos_inf.to_bits());
                return 1;
            }

            float neg_inf = -std::numeric_limits<float>::infinity();
            float16_t fp16_neg_inf(neg_inf);
            if (fp16_neg_inf != float16_t::make_negative_infinity()) {
                fprintf(stderr, "Conversion of 32-bit negative infinity to 16-bit float is %x, not negative infinity.\n", fp16_neg_inf.to_bits());
                return 1;
            }

            Param<float> f_in("f_in");
            f_in.set(pos_inf);
            c = evaluate<float16_t>(cast(Float(16), f_in));
            if (c != float16_t::make_infinity()) {
                fprintf(stderr, "Halide conversion of 32-bit positive infinity to 16-bit float is %x, not positive infinity.\n", c.to_bits());
                return 1;
            }

            f_in.set(neg_inf);
            c = evaluate<float16_t>(cast(Float(16), f_in));
            if (c != float16_t::make_negative_infinity()) {
                fprintf(stderr, "Halide conversion of 32-bit negative infinity to 16-bit float is %x, not negative infinity.\n", c.to_bits());
                return 1;
            }
        }
    }

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    MyCustomErrorReporter reporter;
    set_custom_compile_time_error_reporter(&reporter);

    printf("Testing float16_t...\n");
    if (run_test<float16_t>() != 0) {
        fprintf(stderr, "float16_t test failed!\n");
        return 1;
    }

    printf("Testing _Float16...\n");
#ifdef HALIDE_CPP_COMPILER_HAS_FLOAT16
    if (run_test<_Float16>() != 0) {
        fprintf(stderr, "_Float16 test failed!\n");
        return 1;
    }

#ifdef __clang__
    {
        float16_t f(1.0f16);
        _Float16 f2 = (_Float16)f;
        if (f2 != 1.0f16) {
            fprintf(stderr, "Roundtrip of 16-bit float via _Float16 failed.\n");
            return 1;
        }
    }
#else
    printf("Only clang supports _Float16 constant literal 'f16' suffix, skipping roundtrip test\n");
#endif

#else
    printf("[Compiler does not support _Float16, skipping]\n");
#endif

    printf("Success!\n");
    return 0;
}
