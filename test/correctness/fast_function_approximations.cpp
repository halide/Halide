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
            { "pole-to-pole" , {{-1.57f, 1.57f}}, false, 0,  5, },
            { "extended"     , {{-10.0f, 10.0f}}, false, 0, 50, },
        }
    },
    {
        "atan",
        [](Expr x, Expr y) { return Halide::atan(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_atan(x, prec); },
        {
            { "precise" , {{ -20.0f,  20.0f}}, true, 80, 40 },
            { "extended", {{-200.0f, 200.0f}}, true, 80, 40 },
        }
    },
    {
        "atan2",
        [](Expr x, Expr y) { return Halide::atan2(x, y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_atan2(x, y, prec); },
        {
            { "precise" , {{ -10.0f, 10.0f}, {-10.0f, 10.0f}}, true, 70, 30 },
        }
    },
    {
        "sin",
        [](Expr x, Expr y) { return Halide::sin(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_sin(x, prec); },
        {
            { "-pi/3 to pi/3", {{-pi * 0.333f, pi * 0.333f}}, true, 40, 0 },
            { "-pi/2 to pi/2", {{-pi * 0.5f, pi * 0.5f}}, true, 0, 0 },
            { "-3pi to 3pi",   {{-pi * 3.0f, pi * 3.0f}}, true, 0, 0 },
        }
    },
    {
        "cos",
        [](Expr x, Expr y) { return Halide::cos(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_cos(x, prec); },
        {
            { "-pi/3 to pi/3", {{-pi * 0.333f, pi * 0.333f}}, true, 150, 100 },
            { "-pi/2 to pi/2", {{-pi * 0.5f, pi * 0.5f}}, true, 0, 0 },
            { "-3pi to 3pi",   {{-pi * 3.0f, pi * 3.0f}}, false, 0, 0 },
        }
    },
    {
        "exp",
        [](Expr x, Expr y) { return Halide::exp(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_exp(x, prec); },
        {
            { "precise",  {{0.0f, std::log(2.0f)}}, true , 65, 40 },
            { "extended", {{-20.0f, 20.0f}}       , false, 80, 40 },
        }
    },
    {
        "log",
        [](Expr x, Expr y) { return Halide::log(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_log(x, prec); },
        {
            { "precise",  {{0.76f,    1.49f}}, true , 120, 60 },
            { "extended", {{1e-8f, 20000.0f}}, false, 120, 60 },
        }
    },
    {
        "pow",
        [](Expr x, Expr y) { return Halide::pow(x, y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_pow(x, y, prec); },
        {
            { "precise",  {{0.76f,  1.49f}, {0.0f, std::log(2.0f)}}, true ,   70, 10 },
            { "extended", {{1e-8f,  10.0f}, {-20.0f,        10.0f}}, false, 1200, 80 },
            { "extended", {{1e-8f, 500.0f}, {-20.0f,        10.0f}}, false, 1200, 80 },
        }
    },
    {
        "tanh",
        [](Expr x, Expr y) { return Halide::tanh(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_tanh(x, prec); },
        {
            { "precise"     , {{  -8.0f ,  8.0f }}, true, 2500, 20 },
            { "extended"    , {{ -100.0f, 100.0f}}, true, 2500, 20 },
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
    {ApproximationPrecision{ApproximationPrecision::MULPE, 0, 1e-1, 1}, "MULPE"},
    {ApproximationPrecision{ApproximationPrecision::MULPE, 0, 1e-2, 1}, "MULPE"},
    {ApproximationPrecision{ApproximationPrecision::MULPE, 0, 1e-3, 1}, "MULPE"},
    {ApproximationPrecision{ApproximationPrecision::MULPE, 0, 1e-4, 1}, "MULPE"},
    {ApproximationPrecision{ApproximationPrecision::MULPE, 0, 1e-5, 1}, "MULPE"},
    {ApproximationPrecision{ApproximationPrecision::MULPE, 0, 1e-6, 1}, "MULPE"},
    {ApproximationPrecision{ApproximationPrecision::MULPE, 0, 5e-7, 1}, "MULPE"},

    // MAE
    {{ApproximationPrecision::MAE, 0, 1e-1, 1}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-2, 1}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-3, 1}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-4, 1}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-5, 1}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 1e-6, 1}, "MAE"},
    {{ApproximationPrecision::MAE, 0, 5e-7, 1}, "MAE"},
};

struct ErrorMetrics {
    float max_abs_error{0.0f};
    float max_rel_error{0.0f};
    uint64_t max_ulp_error{0};
    int max_mantissa_error{0};
    float mean_abs_error{0.0f};
    float mean_rel_error{0.0f};
    float mean_ulp_error{0.0f};
};

ErrorMetrics measure_accuracy(Halide::Buffer<float, 1> &out_ref, Halide::Buffer<float, 1> &out_test) {
    ErrorMetrics em{};
    double sum_abs_error = 0;
    double sum_rel_error = 0;
    uint64_t sum_ulp_error = 0;
    uint64_t count = 0;

    for (int i = 0; i < out_ref.width(); ++i) {
        float val_approx = out_test(i);
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
            count++;
            em.max_abs_error = std::max(em.max_abs_error, abs_error);
            em.max_rel_error = std::max(em.max_rel_error, rel_error);
            em.max_ulp_error = std::max(em.max_ulp_error, ulp_error);
            em.max_mantissa_error = std::max(em.max_mantissa_error, mantissa_error);

            sum_abs_error += abs_error;
            sum_rel_error += rel_error;
            sum_ulp_error += ulp_error;
        }
    }

    em.mean_abs_error = float(double(sum_abs_error) / double(count));
    em.mean_rel_error = float(double(sum_rel_error) / double(count));
    em.mean_ulp_error = float(sum_ulp_error / double(count));

    return em;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    setlocale(LC_NUMERIC, "");

    constexpr int steps = 1024;
    Var i{"i"}, x{"x"}, y{"y"};

    Buffer<float, 1> out_ref{steps * steps};
    Buffer<float, 1> out_approx{steps * steps};

    bool use_icons = true;
    const auto &print_ok = [use_icons]() {
        if (use_icons) {
            printf(" ✅");
        } else {
            printf("  ok");
        }
    };
    const auto &print_warn = [use_icons](const char *reason) {
        if (use_icons) {
            printf(" ⚠️[%s]", reason);
        } else {
            printf("  WARN[%s]", reason);
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
    float grace_factor = 1.0f;
    if (target.arch == Target::X86 && !target.has_feature(Halide::Target::FMA) && !target.has_gpu_feature()) {
        grace_factor = 1.05f;
        printf("Using a grace margin of 5%% due to lack of FMA support.\n");
    }

    int num_tests = 0;
    int num_tests_passed = 0;
    for (const FunctionToTest &ftt : functions_to_test) {
        bool skip = false;
        if (argc >= 2) {
            skip = true;
            for (int i = 1; i < argc; ++i) {
                if (argv[i] == ftt.name) {
                    skip = false;
                    break;
                }
            }
        }
        if (skip) {
            printf("Skipping %s\n", ftt.name.c_str());
            continue;
        }

        for (const FunctionToTest::RangedAccuracyTest &rat : ftt.ranged_tests) {
            const TestRange2D &range = rat.range;
            bool is_2d = range.y.l != range.y.u;

            printf("Testing fast_%s on its %s range ", ftt.name.c_str(), rat.name.c_str());
            if (is_2d) {
                printf("([%f, %f] x [%f, %f])...\n", range.x.l, range.x.u, range.y.l, range.y.u);
            } else {
                printf("([%f, %f])...\n", range.x.l, range.x.u);
            }

            Func input{"input"};

            // Prepare the arguments to the functions. We scan over the
            // entire range specified in the table above. Notice how
            // we strict_float() those arguments to make sure we are actually
            // not constant folding those arguments into the expanded
            // polynomial. Note that this strict_float() does not influence
            // the computations of the approximation itself, but only the
            // arguments to the approximated function.
            Expr arg_x, arg_y;
            if (is_2d) {
                Expr tx = x / float(steps);
                Expr ty = y / float(steps);
                input(x, y) = Tuple(
                    range.x.l * (1.0f - tx) + tx * range.x.u,
                    range.y.l * (1.0f - ty) + ty * range.y.u);
                Expr ix = i % steps;
                Expr iy = i / steps;
                arg_x = input(ix, iy)[0];
                arg_y = input(ix, iy)[1];
            } else {
                Expr t = i / float(steps * steps);
                input(i) = range.x.l * (1.0f - t) + t * range.x.u;
                arg_x = input(i);
                // leave arg_y undefined to catch errors.
            }
            input.compute_root();  // Make sure this is super deterministic (computed on always the same CPU).

            // Reference function on CPU
            Func ref_func{ftt.name + "_ref"};
            ref_func(i) = ftt.make_reference(arg_x, arg_y);
            ref_func.realize(out_ref);  // No schedule: scalar evaluation using libm calls on CPU.
            out_ref.copy_to_host();

            // Reference function on device (to check that the "exact" function is exact).
            if (target.has_gpu_feature()) {
                Var io, ii;
                ref_func.never_partition_all();
                // also vectorize to make sure that works on GPU as well...
                ref_func.gpu_tile(i, io, ii, 256, TailStrategy::ShiftInwards).vectorize(ii, 2);
                ref_func.realize(out_approx);
                out_approx.copy_to_host();

#define METRICS_FMT "MaxError{ abs: %.4e , rel: %.4e , ULP: %'14" PRIu64 " , MantissaBits: %2d} | MeanError{ abs: %.4e , ULP: %10.2f}"

                ErrorMetrics em = measure_accuracy(out_ref, out_approx);
                printf("    %s       (native func on device)                   " METRICS_FMT,
                       ftt.name.c_str(),
                       em.max_abs_error, em.max_rel_error, em.max_ulp_error, em.max_mantissa_error,
                       em.mean_abs_error, em.mean_ulp_error);

                if (em.max_ulp_error > 8) {
                    print_warn("Native func is not exact on device.");
                } else {
                    print_ok();
                }
                printf("\n");
            }

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

                ErrorMetrics em = measure_accuracy(out_ref, out_approx);

                printf("    fast_%s  Approx[%6s-optimized, TargetMAE=%.0e] " METRICS_FMT,
                       ftt.name.c_str(), test.objective.c_str(), prec.constraint_max_absolute_error,
                       em.max_abs_error, em.max_rel_error, em.max_ulp_error, em.max_mantissa_error,
                       em.mean_abs_error, em.mean_ulp_error);

                if (test.precision.optimized_for == Halide::ApproximationPrecision::AUTO) {
                    // Make sure that the AUTO is reasonable in at least one way: MAE or Relative/ULP.
                    if (&rat == &ftt.ranged_tests[0]) {
                        // On the first (typically precise) range.
                        num_tests++;
                        if ((em.max_abs_error < 1e-5 || em.max_ulp_error < 20'000 || em.max_rel_error < 1e-2) ||
                            (em.max_abs_error < 1e-4 && em.mean_abs_error < 1e-5 && em.mean_ulp_error < 400)) {
                            num_tests_passed++;
                            print_ok();
                        } else {
                            print_bad("Not precise in any way!");
                        }
                    } else {
                        // On other ranges (typically less precise)
                        num_tests++;
                        if (em.mean_abs_error < 1e-5 || em.mean_ulp_error < 20'000 || em.mean_rel_error < 1e-2) {
                            num_tests_passed++;
                            print_ok();
                        } else {
                            print_bad("Not precise on average in any way!");
                        }
                    }
                } else {
                    if (rat.validate_mae) {
                        num_tests++;
                        if (em.max_abs_error > std::max(prec.constraint_max_absolute_error, best_mae_for_backend) * grace_factor) {
                            print_bad("MaxAbs");
                        } else {
                            print_ok();
                            num_tests_passed++;
                        }
                    } else {
                        // If we don't validate the MAE strictly, let's check if at least it gives
                        // reasonable results when the MAE <= 1e-5 is desired.
                        if (prec.constraint_max_absolute_error != 0 && prec.constraint_max_absolute_error <= 1e-5) {
                            num_tests++;
                            if (em.mean_abs_error < 1e-5 || em.mean_ulp_error < 20'000 || em.mean_rel_error < 1e-2) {
                                num_tests_passed++;
                                print_ok();
                            } else {
                                print_bad("Not precise on average in any way!");
                            }
                        }
                    }
                }

                if (prec.constraint_max_absolute_error != 0
                        && prec.constraint_max_absolute_error <= 1e-5
                        && prec.optimized_for == ApproximationPrecision::MULPE) {
                    if (rat.max_max_ulp_error != 0) {
                        num_tests++;
                        if (em.max_ulp_error > rat.max_max_ulp_error * grace_factor) {
                            print_bad("Max ULP");
                        } else {
                            print_ok();
                            num_tests_passed++;
                        }
                    }
                    if (rat.max_mean_ulp_error != 0) {
                        num_tests++;
                        if (em.mean_ulp_error > rat.max_mean_ulp_error * grace_factor) {
                            print_bad("Mean ULP");
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
