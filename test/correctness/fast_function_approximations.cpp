#include "Halide.h"

#include <cinttypes>
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

uint64_t ulp_diff(float fa, float fb) {
    uint32_t a = Halide::Internal::reinterpret_bits<uint32_t>(fa);
    uint32_t b = Halide::Internal::reinterpret_bits<uint32_t>(fb);
    constexpr uint32_t signbit_mask = 0x80000000;
    int64_t aa = (a & signbit_mask) ? (-int64_t(a & ~signbit_mask)) : (a & ~signbit_mask);
    int64_t bb = (b & signbit_mask) ? (-int64_t(b & ~signbit_mask)) : (b & ~signbit_mask);
    return std::abs(aa - bb);
}

const float pi = 3.14159256f;

struct TestRange {
    float l{0};
    float u{0};
};
struct TestRange2D {
    TestRange x{}, y{};
};

struct FunctionToTest {
    std::string name;
    std::function<Expr(Expr x, Expr y)> make_reference;
    std::function<Expr(Expr x, Expr y, Halide::ApproximationPrecision)> make_approximation;
    struct RangedAccuracyTest {
        std::string name;
        TestRange2D range;
        bool validate_mae{true};
        uint64_t max_max_ulp_error{0};   // When MaxAE-query was 1e-5 or better.
        uint64_t max_mean_ulp_error{0};  // When MaxAE-query was 1e-5 or better.
    };
    std::vector<RangedAccuracyTest> ranged_tests;
} functions_to_test[] = {
    // clang-format off
    {
        "tan",
        [](Expr x, Expr y) { return Halide::tan(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_tan(x, prec); },
        {
            { "close-to-zero", {{-1.05f, 1.05f}}, true , 8,  3, },
            { "pole-to-pole" , {{-1.57f, 1.57f}}, false, 0, 32, },
            { "extended"     , {{-10.0f, 10.0f}}, false, 0, 32, },
        }
    },
    {
        "atan",
        [](Expr x, Expr y) { return Halide::atan(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_atan(x, prec); },
        {
            { "precise" , {{ -20.0f,  20.0f}}, true, 70, 20 },
            { "extended", {{-200.0f, 200.0f}}, true, 70, 20 },
        }
    },
    {
        "atan2",
        [](Expr x, Expr y) { return Halide::atan2(x, y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_atan2(x, y, prec); },
        {
            { "precise" , {{ -10.0f, 10.0f}, {-10.0f, 10.0f}}, true, 70, 20 },
        }
    },
    {
        "sin",
        [](Expr x, Expr y) { return Halide::sin(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_sin(x, prec); },
        {
            { "-pi/3 to pi/3", {{-pi * 0.333f, pi * 0.333f}}, true, 32, 0 },
            { "-pi/2 to pi/2", {{-pi * 0.5f, pi * 0.5f}}, true, 0, 0 },
            { "-3pi to 3pi",   {{-pi * 3.0f, pi * 3.0f}}, false, 0, 0 },
        }
    },
    {
        "cos",
        [](Expr x, Expr y) { return Halide::cos(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_cos(x, prec); },
        {
            { "-pi/3 to pi/3", {{-pi * 0.333f, pi * 0.333f}}, true, 32, 0 },
            { "-pi/2 to pi/2", {{-pi * 0.5f, pi * 0.5f}}, true, 0, 0 },
            { "-3pi to 3pi",   {{-pi * 3.0f, pi * 3.0f}}, false, 0, 0 },
        }
    },
    {
        "exp",
        [](Expr x, Expr y) { return Halide::exp(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_exp(x, prec); },
        {
            { "precise",  {{0.0f, std::log(2.0f)}}, true , 64, 40 },
            { "extended", {{-20.0f, 20.0f}}       , false, 64, 40 },
        }
    },
    {
        "log",
        [](Expr x, Expr y) { return Halide::log(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_log(x, prec); },
        {
            { "precise",  {{0.76f, 1.49f}}, true, 120, 60 },
            { "extended", {{1e-8f, 20000.0f}}, false, 120, 60 },
        }
    },
    {
        "pow",
        [](Expr x, Expr y) { return Halide::pow(x, y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_pow(x, y, prec); },
        {
            { "precise",  {{0.76f,  1.49f}, {0.0f, std::log(2.0f)}}, true , 20, 10 },
            { "extended", {{1e-8f,  10.0f}, {-20.0f,        10.0f}}, false, 20, 10 },
            { "extended", {{1e-8f, 500.0f}, {-20.0f,        10.0f}}, false, 20, 10 },
        }
    },
    {
        "tanh",
        [](Expr x, Expr y) { return Halide::tanh(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_tanh(x, prec); },
        {
            { "precise" , {{ -10.0f, 10.0f}}, true, 70, 20 },
            { "extended" , {{ -100.0f, 100.0f}}, true, 70, 20 },
        }
    },
    // clang-format on
};

struct PrecisionToTest {
    ApproximationPrecision precision;
    std::string objective;
} precisions_to_test[] = {
    // AUTO
    {{}, "AUTO"},

    // MULPE
    {ApproximationPrecision::max_abs_error(1e-1), "MULPE"},
    {ApproximationPrecision::max_abs_error(1e-2), "MULPE"},
    {ApproximationPrecision::max_abs_error(1e-3), "MULPE"},
    {ApproximationPrecision::max_abs_error(1e-4), "MULPE"},
    {ApproximationPrecision::max_abs_error(1e-5), "MULPE"},
    {ApproximationPrecision::max_abs_error(1e-6), "MULPE"},
    {ApproximationPrecision::max_abs_error(5e-7), "MULPE"},

    // MAE
    {{ApproximationPrecision::MAE, 0, 1e-1}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-2}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-3}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-4}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-5}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-6}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 5e-7}, "MAE"},

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
    Var i{"i"};
    // 1D indexing:
    Expr t = i / float(steps * steps);
    // 2D indexing
    Expr ix = i % steps;
    Expr iy = i / steps;
    Expr tx = ix / float(steps);
    Expr ty = iy / float(steps);
    Buffer<float> out_ref{steps * steps};
    Buffer<float> out_approx{steps * steps};

    bool use_icons = true;
    const auto &print_ok = [use_icons]() {
        if (use_icons) {
            printf(" ✅");
        } else {
            printf("  ok");
        }
    };
    const auto &print_bad = [use_icons](const char *reason) {
        if (use_icons) {
            printf(" ❌[%s]", reason);
        } else {
            printf("  BAD[%s]", reason);
        }
    };

    float best_mae_for_backend = 0.0f;
    if (target.has_feature(Halide::Target::Vulkan)) {
        best_mae_for_backend = 1e-6f;
        printf("Vulkan backend detected: Reducing required maximal absolute error to %e.\n", best_mae_for_backend);
    }

    int num_tests = 0;
    int num_tests_passed = 0;
    for (const FunctionToTest &ftt : functions_to_test) {
        if (argc == 2 && argv[1] != ftt.name) {
            printf("Skipping %s\n", ftt.name.c_str());
            continue;
        }

        for (const FunctionToTest::RangedAccuracyTest &rat : ftt.ranged_tests) {
            const TestRange2D &range = rat.range;
            printf("Testing fast_%s on its %s range ([%f, %f], [%f, %f])...\n",
                   ftt.name.c_str(), rat.name.c_str(),
                   range.x.l, range.x.u, range.y.l, range.y.u);

            bool is_2d = range.y.l != range.y.u;

            // Prepare the arguments to the functions. We scan over the
            // entire range specified in the table above. Notice how
            // we strict_float() those arguments to make sure we are actually
            // not constant folding those arguments into the expanded
            // polynomial. Note that this strict_float() does not influence
            // the computations of the approximation itself, but only the
            // arguments to the approximated function.
            Expr arg_x, arg_y;
            if (is_2d) {
                arg_x = strict_float(range.x.l * (1.0f - tx) + range.x.u * tx);
                arg_y = strict_float(range.y.l * (1.0f - ty) + range.y.u * ty);
            } else {
                arg_x = strict_float(range.x.l * (1.0f - t) + range.x.u * t);
                // leave arg_y undefined to catch errors.
            }

            // Reference:
            Func ref_func{ftt.name + "_ref"};
            ref_func(i) = ftt.make_reference(arg_x, arg_y);
            ref_func.realize(out_ref);  // No schedule: scalar evaluation using libm calls on CPU.
            out_ref.copy_to_host();

            // Approximations:
            for (const PrecisionToTest &test : precisions_to_test) {
                Halide::ApproximationPrecision prec = test.precision;
                Func approx_func{ftt.name + "_approx"};
                approx_func(i) = ftt.make_approximation(arg_x, arg_y, prec);

                if (target.has_gpu_feature()) {
                    Var io, ii;
                    approx_func.never_partition_all();
                    approx_func.gpu_tile(i, io, ii, 256, TailStrategy::ShiftInwards);
                } else {
                    approx_func.vectorize(i, 8);
                }
                approx_func.realize(out_approx);
                out_approx.copy_to_host();

                float max_abs_error = 0.0f;
                float max_rel_error = 0.0f;
                uint64_t max_ulp_error = 0;
                int max_mantissa_error = 0;
                double sum_abs_error = 0;
                double sum_rel_error = 0;
                uint64_t sum_ulp_error = 0;

                for (int i = 0; i < steps * steps; ++i) {
                    float val_approx = out_approx(i);
                    float val_ref = out_ref(i);
                    float abs_error = std::abs(val_approx - val_ref);
                    float rel_error = abs_error / (std::abs(val_ref) + 1e-7);
                    int mantissa_error = bits_diff(val_ref, val_approx);
                    uint64_t ulp_error = ulp_diff(val_ref, val_approx);

                    if (!std::isfinite(abs_error)) {
                        if (val_ref != val_approx) {
                            std::printf("      Warn: %.10e vs %.10e\n", val_ref, val_approx);
                        }
                    } else {
                        if (ulp_error > 100'000) {
                            // std::printf("\nExtreme ULP error %d: %.10e vs %.10e", ulp_error, val_ref, val_approx);
                        }
                        max_abs_error = std::max(max_abs_error, abs_error);
                        max_rel_error = std::max(max_rel_error, rel_error);
                        max_ulp_error = std::max(max_ulp_error, ulp_error);
                        max_mantissa_error = std::max(max_mantissa_error, mantissa_error);

                        sum_abs_error += abs_error;
                        sum_rel_error += rel_error;
                        sum_ulp_error += ulp_error;
                    }
                }

                float mean_abs_error = float(double(sum_abs_error) / double(steps * steps));
                float mean_rel_error = float(double(sum_rel_error) / double(steps * steps));
                float mean_ulp_error = float(sum_ulp_error / double(steps * steps));

                printf("    fast_%s  Approx[%s-optimized, TargetMAE=%.0e] MaxError{ abs: %.4e | rel: %.4e | ULP: %'14" PRIu64 " | MantissaBits: %2d}   MeanError{ abs: %.4e | ULP: %10.1f}",
                       ftt.name.c_str(), test.objective.c_str(), prec.constraint_max_absolute_error,
                       max_abs_error, max_rel_error, max_ulp_error, max_mantissa_error,
                       mean_abs_error, mean_ulp_error);

                if (test.precision.optimized_for == Halide::ApproximationPrecision::AUTO) {
                    // Make sure that the AUTO is reasonable in at least one way: MAE or Relative/ULP.
                    if (&rat == &ftt.ranged_tests[0]) {
                        // On the first (typically precise) range.
                        num_tests++;
                        if (max_abs_error < 1e-5 || max_ulp_error < 20'000 || max_rel_error < 1e-2) {
                            num_tests_passed++;
                            print_ok();
                        } else {
                            print_bad("Not precise in any way!");
                        }
                    } else {
                        // On other ranges (typically less precise)
                        num_tests++;
                        if (mean_abs_error < 1e-5 || mean_ulp_error < 20'000 || mean_rel_error < 1e-2) {
                            num_tests_passed++;
                            print_ok();
                        } else {
                            print_bad("Not precise on average in any way!");
                        }
                    }
                } else {
                    if (rat.validate_mae) {
                        num_tests++;
                        if (max_abs_error > std::max(prec.constraint_max_absolute_error, best_mae_for_backend)) {
                            print_bad("MaxAbsErr too big!");
                        } else {
                            print_ok();
                            num_tests_passed++;
                        }
                    } else {
                        // If we don't validate the MAE strictly, let's check if at least it gives
                        // reasonable results when the MAE <= 1e-5 is desired.
                        if (prec.constraint_max_absolute_error != 0 && prec.constraint_max_absolute_error <= 1e-5) {
                            num_tests++;
                            if (mean_abs_error < 1e-5 || mean_ulp_error < 20'000 || mean_rel_error < 1e-2) {
                                num_tests_passed++;
                                print_ok();
                            } else {
                                print_bad("Not precise on average in any way!");
                            }
                        }
                    }
                }

                if (prec.constraint_max_absolute_error != 0 && prec.constraint_max_absolute_error <= 1e-5 && prec.optimized_for == ApproximationPrecision::MULPE) {
                    if (rat.max_max_ulp_error != 0) {
                        num_tests++;
                        if (max_ulp_error > rat.max_max_ulp_error) {
                            print_bad("Max ULP Error too big!!");
                        } else {
                            print_ok();
                            num_tests_passed++;
                        }
                    }
                    if (rat.max_mean_ulp_error != 0) {
                        num_tests++;
                        if (mean_ulp_error > rat.max_mean_ulp_error) {
                            print_bad("Mean ULP Error too big!!");
                        } else {
                            print_ok();
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
    if (num_tests_passed < num_tests) {
        printf("Not all accuracy tests passed.\n");
        return 1;
    }
    printf("Success!\n");
    return 0;
}
