#include "Halide.h"

#include <locale.h>

using namespace Halide;

int bits_diff(float fa, float fb) {
    uint32_t a = Halide::Internal::reinterpret_bits<uint32_t>(fa);
    uint32_t b = Halide::Internal::reinterpret_bits<uint32_t>(fb);
    uint32_t a_exp = a >> 23;
    uint32_t b_exp = b >> 23;
    if (a_exp != b_exp) return -100;
    uint32_t diff = a > b ? a - b : b - a;
    int count = 0;
    while (diff) {
        count++;
        diff /= 2;
    }
    return count;
}

int ulp_diff(float fa, float fb) {
    uint32_t a = Halide::Internal::reinterpret_bits<uint32_t>(fa);
    uint32_t b = Halide::Internal::reinterpret_bits<uint32_t>(fb);
    return std::abs(int64_t(a) - int64_t(b));
}

const float pi = 3.14159256f;

struct TestRange {
    float l, u;
};
struct TestRange2D {
    TestRange x, y;
};

constexpr int VALIDATE_MAE_ON_PRECISE = 0x1;
constexpr int VALIDATE_MAE_ON_EXTENDED = 0x2;

struct FunctionToTest {
    std::string name;
    TestRange2D precise;
    TestRange2D extended;
    std::function<Expr(Expr x, Expr y)> make_reference;
    std::function<Expr(Expr x, Expr y, Halide::ApproximationPrecision)> make_approximation;
    int max_mulpe_precise{0}; // max MULPE allowed when MAE query was <= 1e-6
    int max_mulpe_extended{0}; // max MULPE allowed when MAE query was <= 1e-6
    int test_bits{0xff};
} functions_to_test[] = {
    // clang-format off
    {
        "atan",
        {{-20.0f, 20.0f}, {-0.1f, 0.1f}},
        {{-200.0f, 200.0f}, {-0.1f, 0.1f}},
        [](Expr x, Expr y) { return Halide::atan(x + y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_atan(x + y, prec); },
        12, 12,
    },
    {
        "atan2",
        {{-1.0f, 1.0f}, {-0.1f, 0.1f}},
        {{-10.0f, 10.0f}, {-10.0f, 10.0f}},
        [](Expr x, Expr y) { return Halide::atan2(x, y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_atan2(x, y, prec); },
        12, 70,
    },
    {
        "sin",
        {{-pi * 0.5f, pi * 0.5f}, {-0.1f, -0.1f}},
        {{-3 * pi, 3 * pi}, {-0.5f, 0.5f}},
        [](Expr x, Expr y) { return Halide::sin(x + y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_sin(x + y, prec); },
    },
    {
        "cos",
        {{-pi * 0.5f, pi * 0.5f}, {-0.1f, -0.1f}},
        {{-3 * pi, 3 * pi}, {-0.5f, 0.5f}},
        [](Expr x, Expr y) { return Halide::cos(x + y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_cos(x + y, prec); },
    },
    {
        "exp",
        {{0.0f, std::log(2.0f)}, {-0.1f, -0.1f}},
        {{-20.0f, 20.0f}, {-0.5f, 0.5f}},
        [](Expr x, Expr y) { return Halide::exp(x + y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_exp(x + y, prec); },
        5, 20,
        VALIDATE_MAE_ON_PRECISE,
    },
    {
        "log",
        {{0.76f, 1.49f}, {-0.01f, -0.01f}},
        {{1e-8f, 20000.0f}, {-1e-9f, 1e-9f}},
        [](Expr x, Expr y) { return Halide::log(x + y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_log(x + y, prec); },
        20, 20,
        VALIDATE_MAE_ON_PRECISE,
    },
    // clang-format on
};

struct PrecisionToTest {
    ApproximationPrecision precision;
    std::string objective;
    float expected_mae{0.0f};
} precisions_to_test[] = {
    // MSE
    {{ApproximationPrecision::MSE, 0, 1e-1}, "MSE"},
    {{ApproximationPrecision::MSE, 0, 1e-2}, "MSE"},
    {{ApproximationPrecision::MSE, 0, 1e-3}, "MSE"},
    {{ApproximationPrecision::MSE, 0, 1e-4}, "MSE"},
    {{ApproximationPrecision::MSE, 0, 1e-5}, "MSE"},
    {{ApproximationPrecision::MSE, 0, 1e-6}, "MSE"},
    {{ApproximationPrecision::MSE, 0, 5e-7}, "MSE"},

    // MAE
    {{ApproximationPrecision::MAE, 0, 1e-1}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-2}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-3}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-4}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-5}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-6}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 5e-7}, "MAE"},

    // MULPE
    {{ApproximationPrecision::MULPE, 0, 1e-1}, "MULPE"},
    {{ApproximationPrecision::MULPE, 0, 1e-2}, "MULPE"},
    {{ApproximationPrecision::MULPE, 0, 1e-3}, "MULPE"},
    {{ApproximationPrecision::MULPE, 0, 1e-4}, "MULPE"},
    {{ApproximationPrecision::MULPE, 0, 1e-5}, "MULPE"},
    {{ApproximationPrecision::MULPE, 0, 1e-6}, "MULPE"},
    {{ApproximationPrecision::MULPE, 0, 5e-7}, "MULPE"},

    // MULPE + MAE
    {{ApproximationPrecision::MULPE_MAE, 0, 1e-1}, "MULPE+MAE"},
    {{ApproximationPrecision::MULPE_MAE, 0, 1e-2}, "MULPE+MAE"},
    {{ApproximationPrecision::MULPE_MAE, 0, 1e-3}, "MULPE+MAE"},
    {{ApproximationPrecision::MULPE_MAE, 0, 1e-4}, "MULPE+MAE"},
    {{ApproximationPrecision::MULPE_MAE, 0, 1e-5}, "MULPE+MAE"},
    {{ApproximationPrecision::MULPE_MAE, 0, 1e-6}, "MULPE+MAE"},
    {{ApproximationPrecision::MULPE_MAE, 0, 5e-7}, "MULPE+MAE"},
};


int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    setlocale(LC_NUMERIC, "");

    constexpr int steps = 1024;
    Var x{"x"}, y{"y"};
    Expr t0 = x / float(steps);
    Expr t1 = y / float(steps);
    Buffer<float> out_ref{steps, steps};
    Buffer<float> out_approx{steps, steps};

    int num_tests = 0;
    int num_tests_passed = 0;
    for (const FunctionToTest &ftt : functions_to_test) {
        if (argc == 2 && argv[1] != ftt.name) {
            printf("Skipping %s\n", ftt.name.c_str());
            continue;
        }

        const float min_precision_extended = 5e-6;
        std::pair<TestRange2D, std::string> ranges[2] = {{ftt.precise, "precise"}, {ftt.extended, "extended"}};
        for (const std::pair<TestRange2D, std::string> &test_range_and_name : ranges) {
            TestRange2D range = test_range_and_name.first;
            printf("Testing fast_%s on its %s range ([%f, %f], [%f, %f])...\n", ftt.name.c_str(), test_range_and_name.second.c_str(),
                    range.x.l, range.x.u, range.y.l, range.y.u);
            // Reference:
            Expr arg_x = range.x.l * (1.0f - t0) + range.x.u * t0;
            Expr arg_y = range.y.l * (1.0f - t1) + range.y.u * t1;
            Func ref_func{ftt.name + "_ref"};
            ref_func(x, y) = ftt.make_reference(arg_x, arg_y);
            ref_func.realize(out_ref); // No schedule: scalar evaluation using libm calls on CPU.
            out_ref.copy_to_host();
            for (const PrecisionToTest &test : precisions_to_test) {
                Halide::ApproximationPrecision prec = test.precision;
                prec.allow_native_when_faster = false; // We want to actually validate our approximation.

                Func approx_func{ftt.name + "_approx"};
                approx_func(x, y) = ftt.make_approximation(arg_x, arg_y, prec);

                if (target.has_gpu_feature()) {
                    Var xo, xi;
                    Var yo, yi;
                    approx_func.never_partition_all();
                    approx_func.gpu_tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::ShiftInwards);
                } else {
                    approx_func.vectorize(x, 8);
                }
                approx_func.realize(out_approx);
                out_approx.copy_to_host();

                float max_absolute_error = 0.0f;
                int max_ulp_error = 0;
                int max_mantissa_error = 0;

                for (int y = 0; y < steps; ++y) {
                    for (int x = 0; x < steps; ++x) {
                        float val_approx = out_approx(x, y);
                        float val_ref = out_ref(x, y);
                        float abs_diff = std::abs(val_approx - val_ref);
                        int mantissa_error = bits_diff(val_ref, val_approx);
                        int ulp_error = ulp_diff(val_ref, val_approx);

                        max_absolute_error = std::max(max_absolute_error, abs_diff);
                        max_mantissa_error = std::max(max_mantissa_error, mantissa_error);
                        max_ulp_error = std::max(max_ulp_error, ulp_error);
                    }
                }

                printf("    fast_%s  Approx[%s-optimized, TargetMAE=%.0e] | MaxAbsError: %.4e | MaxULPError: %'14d | MaxMantissaError: %2d",
                        ftt.name.c_str(), test.objective.c_str(), prec.constraint_max_absolute_error,
                        max_absolute_error, max_ulp_error, max_mantissa_error);

                if (test_range_and_name.second == "precise") {
                    if ((ftt.test_bits & VALIDATE_MAE_ON_PRECISE)) {
                        num_tests++;
                        if (max_absolute_error > prec.constraint_max_absolute_error) {
                            printf("  BAD: MaxAbsErr too big!");
                        } else {
                            printf("  ok");
                            num_tests_passed++;
                        }
                    }
                    if (ftt.max_mulpe_precise != 0 && prec.constraint_max_absolute_error <= 1e-6 && prec.optimized_for == ApproximationPrecision::MULPE) {
                        num_tests++;
                        if (max_ulp_error > ftt.max_mulpe_precise) {
                            printf("  BAD: MULPE too big!!");
                        } else {
                            printf("  ok");
                            num_tests_passed++;
                        }
                    }
                } else if (test_range_and_name.second == "extended") {
                    if ((ftt.test_bits & VALIDATE_MAE_ON_EXTENDED)) {
                        num_tests++;
                        if (max_absolute_error > std::max(prec.constraint_max_absolute_error, min_precision_extended)) {
                            printf("  BAD: MaxAbsErr too big!");
                        } else {
                            printf("  ok");
                            num_tests_passed++;
                        }
                    }
                    if (ftt.max_mulpe_extended != 0 && prec.constraint_max_absolute_error <= 1e-6 && prec.optimized_for == ApproximationPrecision::MULPE) {
                        num_tests++;
                        if (max_ulp_error > ftt.max_mulpe_extended) {
                            printf("  BAD: MULPE too big!!");
                        } else {
                            printf("  ok");
                            num_tests_passed++;
                        }
                    }
                }
                printf("\n");
            }
        }
        printf("\n");
    }
    printf("Passed %d / %d accuracy tests.\n", num_tests_passed, num_tests);
    printf("Success!\n");
}

