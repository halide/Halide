#include "Halide.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <locale.h>
#include <string>

using namespace Halide;
using namespace Halide::Internal;

const bool use_icons = true;
const auto &print_ok = []() {
    if (use_icons) {
        printf(" ✅");
    } else {
        printf("  ok");
    }
};
const auto &print_warn = [](const char *reason) {
    if (use_icons) {
        printf(" ⚠️[%s]", reason);
    } else {
        printf("  WARN[%s]", reason);
    }
};
const auto &print_bad = [](const char *reason) {
    if (use_icons) {
        printf(" ❌[%s]", reason);
    } else {
        printf("  BAD[%s]", reason);
    }
};

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

const float pi_d = 3.14159265358979323846;
const float pi = pi_d;
const float just_not_pi_over_two = std::nexttoward(std::nexttoward(float(pi_d / 2), 0.0f), 0.0f);

struct TestRange {
    float l{0};
    float u{0};
};
struct TestRange2D {
    TestRange x{}, y{};
};

struct RangedAccuracyTest {
    std::string name;
    TestRange2D range;
    struct Validation {
        double factor{1.0};
        double term{0.0};
        operator bool() const {
            return factor != 0.0 || term != 0.0;
        }

        void eval(const char *str, double expected_error, double actual_error, int &num_tests, int &num_tests_passed) const {
            if (factor != 0 || term != 0.0) {
                num_tests++;
                if (expected_error * factor + term < actual_error) {
                    print_bad(str);
                    printf(" %g > %g ", actual_error, expected_error);
                    if (factor != 1.0) {
                        printf("* %f ", factor);
                    }
                    if (term != 0.0) {
                        printf("+ %g ", term);
                    }
                    printf(" ");
                } else {
                    print_ok();
                    num_tests_passed++;
                }
            }
        }
    } max_abs, mean_abs, max_ulp, mean_ulp;

    uint64_t max_max_ulp_error{0};   // When MaxAE-query was 1e-5 or better and forced poly.
    uint64_t max_mean_ulp_error{0};  // When MaxAE-query was 1e-5 or better and forced poly.

    bool requires_strict_float{false};
};

constexpr RangedAccuracyTest::Validation no_val = {0.0, 0.0};

constexpr RangedAccuracyTest::Validation rlx_abs_val = {1.02, 1e-7};
constexpr RangedAccuracyTest::Validation vrlx_abs_val = {1.1, 1e-6};
constexpr RangedAccuracyTest::Validation rsnbl_abs_val = {2.0, 1e-5};
constexpr RangedAccuracyTest::Validation rlx_abs_val_pct(double pct) {
    return {1.0 + 100 * pct, 1e-7};
}
constexpr RangedAccuracyTest::Validation max_abs_val(double max_val) {
    return {0.0f, max_val};
}

constexpr RangedAccuracyTest::Validation rlx_ulp_val = {1.01, 20};
constexpr RangedAccuracyTest::Validation vrlx_ulp_val = {1.1, 200};
constexpr RangedAccuracyTest::Validation rsnbl_ulp_val = {20.0, 1'000};

Expr makeshift_expm1(Expr x) {
    Type t = x.type();
    Expr r = x;
    Expr xpow = x;
    int factr = 1;
    for (int i = 2; i < 15; ++i) {
        xpow = xpow * x;
        factr *= i;
        r += xpow * Halide::Internal::make_const(t, 1.0 / factr);
    }
    Expr ivl = Halide::Internal::make_const(t, 1.0);
    return select(x > -ivl && x < ivl, r, exp(x) - make_const(t, 1.0));
}

struct FunctionToTest {
    std::string name;
    Call::IntrinsicOp fast_op;
    std::function<Expr(Expr x, Expr y)> make_reference;
    std::function<Expr(Expr x, Expr y, Halide::ApproximationPrecision)> make_approximation;
    const Halide::Internal::Approximation *(*obtain_approximation)(Halide::ApproximationPrecision, Halide::Type);
    std::vector<RangedAccuracyTest> ranged_tests;
} functions_to_test[] = {
    // clang-format off
    {
        "tan", Call::fast_tan,
        [](Expr x, Expr y) { return Halide::tan(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_tan(x, prec); },
        Halide::Internal::ApproximationTables::best_tan_approximation,
        {
            { "close-to-zero", {{-0.78f, 0.78f}}              , {}, {}, {}, {}, 40,  5, },
            { "pole-to-pole" , {{-0.0f, just_not_pi_over_two}}, no_val, no_val, {1.01, 4}, rsnbl_ulp_val, 40,  5, true},
            { "extended"     , {{-10.0f, 10.0f}}              , no_val, no_val, no_val, rsnbl_ulp_val,  0, 50, },
        }
    },
    {
        "atan", Call::fast_atan,
        [](Expr x, Expr y) { return Halide::atan(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_atan(x, prec); },
        Halide::Internal::ApproximationTables::best_atan_approximation,
        {
            { "precise" , {{ -20.0f,  20.0f}}, {}, {}, {}, {}, 80, 40 },
            { "extended", {{-200.0f, 200.0f}}, {}, {}, {}, {}, 80, 40 },
        }
    },
    {
        "atan2", Call::fast_atan2,
        [](Expr x, Expr y) { return Halide::atan2(x, y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_atan2(x, y, prec); },
        Halide::Internal::ApproximationTables::best_atan_approximation,
        {
            { "precise" , {{ -10.0f, 10.0f}, {-10.0f, 10.0f}}, rlx_abs_val_pct(4), rlx_abs_val, rlx_ulp_val, rlx_ulp_val, 70, 30 },
        }
    },
    {
        "sin", Call::fast_sin,
        [](Expr x, Expr y) { return Halide::sin(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_sin(x, prec); },
        Halide::Internal::ApproximationTables::best_sin_approximation,
        {
            { "-pi/3 to pi/3", {{-pi * 0.333f, pi * 0.333f}}                  , {}, {}, {}, {}, 40, 0 },
            { "-pi/2 to pi/2", {{-just_not_pi_over_two, just_not_pi_over_two}}, {}, {}, {}, {}, 0, 0 },
            { "-10 to 10",   {{-10.0f, 10.0f}}                                , rsnbl_abs_val, rsnbl_abs_val, no_val, rsnbl_ulp_val, 0, 0 },
        }
    },
    {
        "cos", Call::fast_cos,
        [](Expr x, Expr y) { return Halide::cos(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_cos(x, prec); },
        Halide::Internal::ApproximationTables::best_cos_approximation,
        {
            // We have to relax all tests here, because it actually compiles to a sin, so the table entries are not accurate.
            { "-pi/3 to pi/3", {{-pi * 0.333f, pi * 0.333f}}, rlx_abs_val, rlx_abs_val, rlx_ulp_val, rlx_ulp_val, 150, 100 },
            { "-pi/2 to pi/2", {{-just_not_pi_over_two, just_not_pi_over_two}}, rlx_abs_val, rlx_abs_val, no_val, rsnbl_ulp_val, 0, 0, true},
            { "-10 to 10",   {{-10.0f, 10.0f}}, rsnbl_abs_val, rsnbl_abs_val, no_val, rsnbl_ulp_val, 0, 0 },
        }
    },
    {
        "expm1", Call::fast_expm1,
        [](Expr x, Expr y) { return makeshift_expm1(x); }, // We don't have expm1... :(
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_expm1(x, prec); },
        Halide::Internal::ApproximationTables::best_expm1_approximation,
        {
            { "precise",  {{-0.5f * std::log(2.0f)), 0.5f * std::log(2.0f))}}, {}, {}, {}, {}, 300, 130 },
            { "extended", {{-20.0f, 20.0f}}, no_val, no_val, rsnbl_ulp_val, rlx_ulp_val, 600, 40 },
        }
    },
    {
        "exp", Call::fast_exp,
        [](Expr x, Expr y) { return Halide::exp(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_exp(x, prec); },
        Halide::Internal::ApproximationTables::best_exp_approximation,
        {
            { "precise",  {{0.0f, std::log(2.0f)}}, {}, {}, {}, {}, 65, 40 },
            { "extended", {{-20.0f, 20.0f}}       , no_val, no_val, rlx_ulp_val, rlx_ulp_val, 80, 40 },
        }
    },
    {
        "log", Call::fast_log,
        [](Expr x, Expr y) { return Halide::log(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_log(x, prec); },
        Halide::Internal::ApproximationTables::best_log_approximation,
        {
            { "precise",  {{0.76f,    1.49f}}, {}, {}, {}, {}, 2500, 1000 },
            { "extended", {{1e-8f, 20000.0f}}, rsnbl_abs_val, rsnbl_abs_val, rsnbl_ulp_val, rsnbl_ulp_val, 2500, 60 },
        }
    },
    {
        "pow", Call::fast_pow,
        [](Expr x, Expr y) { return Halide::pow(x, y); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_pow(x, y, prec); },
        nullptr,
        {
            { "precise",  {{0.76f,  1.49f}, {0.0f, std::log(2.0f)}}, {}, {}, {}, {},   50,  10 },
            { "extended", {{1e-8f,  10.0f}, {  0.0f,        10.0f}}, no_val, no_val, no_val, no_val,    0, 140 },
            { "extended", {{1e-8f,  50.0f}, {-20.0f,        10.0f}}, no_val, no_val, no_val, no_val,    0, 140 },
        }
    },
    {
        "tanh", Call::fast_tanh,
        [](Expr x, Expr y) { return Halide::tanh(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_tanh(x, prec); },
        nullptr,
        {
            { "precise"     , {{  -8.0f ,  8.0f }}, {}, {}, {}, {}, 2500, 20 },
            { "extended"    , {{ -100.0f, 100.0f}}, no_val, no_val, no_val, no_val, 2500, 20 },
        }
    },
    {
        "asin", Call::fast_asin,
        [](Expr x, Expr y) { return Halide::asin(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_asin(x, prec); },
        Halide::Internal::ApproximationTables::best_atan_approximation, // Yes, atan table!
        {
            { "precise"     , {{  -1.0f ,  1.0f }}, vrlx_abs_val, vrlx_abs_val, vrlx_ulp_val, vrlx_ulp_val, 2500, 50 },
        }
    },
    {
        "acos", Call::fast_acos,
        [](Expr x, Expr y) { return Halide::acos(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_acos(x, prec); },
        Halide::Internal::ApproximationTables::best_atan_approximation, // Yes, atan table!
        {
            { "precise"     , {{  -1.0f ,  1.0f }}, vrlx_abs_val, vrlx_abs_val, vrlx_ulp_val, vrlx_ulp_val, 2500, 50 },
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

    // MULPE (forced Poly)
    {ApproximationPrecision::poly_mulpe(1), "MULPE"},
    {ApproximationPrecision::poly_mulpe(2), "MULPE"},
    {ApproximationPrecision::poly_mulpe(3), "MULPE"},
    {ApproximationPrecision::poly_mulpe(4), "MULPE"},
    {ApproximationPrecision::poly_mulpe(5), "MULPE"},
    {ApproximationPrecision::poly_mulpe(6), "MULPE"},
    {ApproximationPrecision::poly_mulpe(7), "MULPE"},
    {ApproximationPrecision::poly_mulpe(8), "MULPE"},

    // MAE (forced Poly)
    {ApproximationPrecision::poly_mae(1), "MAE"},
    {ApproximationPrecision::poly_mae(2), "MAE"},
    {ApproximationPrecision::poly_mae(3), "MAE"},
    {ApproximationPrecision::poly_mae(4), "MAE"},
    {ApproximationPrecision::poly_mae(5), "MAE"},
    {ApproximationPrecision::poly_mae(6), "MAE"},
    {ApproximationPrecision::poly_mae(7), "MAE"},
    {ApproximationPrecision::poly_mae(8), "MAE"},

    // With minimum precision
    {{ApproximationPrecision::OptimizationObjective::MAE, 0, 1e-5f, 0}, "MAE"},
    {{ApproximationPrecision::OptimizationObjective::MULPE, 0, 1e-5f, 0}, "MULPE"},
    {{ApproximationPrecision::OptimizationObjective::MAE, 0, 1e-5f, 1}, "MAE"},
    {{ApproximationPrecision::OptimizationObjective::MULPE, 0, 1e-5f, 1}, "MULPE"},
};

struct ErrorMetrics {
    float max_abs_error{0.0f};
    float max_rel_error{0.0f};
    uint64_t max_ulp_error{0};
    int max_mantissa_error{0};
    float mean_abs_error{0.0f};
    float mean_rel_error{0.0f};
    float mean_ulp_error{0.0f};

    struct Worst {
        float actual{0.0f};
        float expected{0.0f};
        int where{0};
    } worst_abs, worst_ulp;
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

            if (abs_error > em.max_abs_error) {
                em.worst_abs.actual = val_approx;
                em.worst_abs.expected = val_ref;
                em.worst_abs.where = i;
            }
            if (ulp_error > em.max_ulp_error) {
                em.worst_ulp.actual = val_approx;
                em.worst_ulp.expected = val_ref;
                em.worst_ulp.where = i;
            }

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

    Buffer<float, 1> out_input_0{steps * steps};
    Buffer<float, 1> out_input_1{steps * steps};
    Buffer<float, 1> out_ref{steps * steps};
    Buffer<float, 1> out_approx{steps * steps};

    bool target_has_proper_strict_float_support = !target.has_gpu_feature();

    double best_mae_for_backend = 0.0;
    if (target.has_feature(Halide::Target::Vulkan)) {
        best_mae_for_backend = 1e-6;
        printf("Vulkan backend detected: Reducing required maximal absolute error to %e.\n", best_mae_for_backend);
    }

    bool emit_asm = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--asm") == 0) {
            emit_asm = true;
            break;
        }
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

        for (const RangedAccuracyTest &rat : ftt.ranged_tests) {
            const TestRange2D &range = rat.range;
            bool is_2d = range.y.l != range.y.u;

            printf("Testing fast_%s on its %s range (", ftt.name.c_str(), rat.name.c_str());
            printf("[%g, %g]", range.x.l, range.x.u);
            if (is_2d) {
                printf(" x [%g, %g]", range.y.l, range.y.u);
            }
            printf(")...\n");

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
                Expr ix = i % steps;
                Expr iy = i / steps;
                Expr tx = ix / float(steps);
                Expr ty = iy / float(steps);
                input(i) = Tuple(
                    range.x.l * (1.0f - tx) + tx * range.x.u,
                    range.y.l * (1.0f - ty) + ty * range.y.u);
                arg_x = input(i)[0];
                arg_y = input(i)[1];
            } else {
                Expr t = i / float(steps * steps);
                input(i) = range.x.l * (1.0f - t) + t * range.x.u;
                arg_x = input(i);
                // leave arg_y undefined to catch errors.
            }
            input.compute_root();  // Make sure this is super deterministic (computed on always the same CPU).

            // Reference function on CPU
            Func ref_func{ftt.name + "_ref_cpu_via_double"};
            ref_func(i) = cast<float>(ftt.make_reference(
                cast<double>(arg_x),
                arg_y.defined() ? cast<double>(arg_y) : arg_y));
            // No schedule: scalar evaluation using libm calls on CPU.
            Pipeline pl{{ref_func, input}};
            if (is_2d) {
                pl.realize({out_ref, out_input_0, out_input_1});
            } else {
                pl.realize({out_ref, out_input_0});
            }
            out_ref.copy_to_host();

            // Reference function on device (to check that the "exact" function is exact).
            if (target.has_gpu_feature()) {
                Var io, ii;
                Func ref_func_gpu{ftt.name + "_ref_gpu"};
                ref_func_gpu(i) = ftt.make_reference(arg_x, arg_y);
                ref_func_gpu.never_partition_all();
                // also vectorize to make sure that works on GPU as well...
                ref_func_gpu.gpu_tile(i, io, ii, 256, TailStrategy::ShiftInwards).vectorize(ii, 2);
                ref_func_gpu.realize(out_approx);
                out_approx.copy_to_host();

#define METRICS_FMT "MaxError{ abs: %.4e , rel: %.4e , ULP: %14" PRIu64 " , MantissaBits: %2d} | MeanError{ abs: %.4e , ULP: %10.2f}"

                ErrorMetrics em = measure_accuracy(out_ref, out_approx);
                printf("    %s       (native func on device)                              " METRICS_FMT,
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
                if (prec.force_halide_polynomial == 0 && prec.optimized_for != Halide::ApproximationPrecision::AUTO) {
                    if (!fast_math_func_has_intrinsic_based_implementation(ftt.fast_op, target.get_required_device_api(), target)) {
                        // Skip it, it doesn't have an alternative intrinsics-based version.
                        // It would compile to the same polynomials we just tested.
                        continue;
                    }
                }

                std::string name = ftt.name + "_approx";
                name += "_" + test.objective;
                name += "_poly" + std::to_string(test.precision.force_halide_polynomial);
                Func approx_func{name};
                approx_func(i) = ftt.make_approximation(arg_x, arg_y, prec);

                approx_func.align_bounds(i, 8);
                if (target.has_gpu_feature()) {
                    Var io, ii;
                    approx_func.never_partition_all();
                    approx_func.gpu_tile(i, io, ii, 256, TailStrategy::ShiftInwards);
                } else {
                    approx_func.vectorize(i, 8);
                }
                approx_func.realize(out_approx);
                if (emit_asm) {
                    approx_func.compile_to_assembly(approx_func.name() + ".asm", {out_approx},
                                                    target.with_feature(Halide::Target::NoAsserts)
                                                        .with_feature(Halide::Target::NoBoundsQuery)
                                                        .with_feature(Halide::Target::NoRuntime));
                }
                out_approx.copy_to_host();

                ErrorMetrics em = measure_accuracy(out_ref, out_approx);

                printf("    fast_%s  Approx[Obj=%6s, TargetMAE=%.0e, %15s] " METRICS_FMT,
                       ftt.name.c_str(), test.objective.c_str(), prec.constraint_max_absolute_error,
                       prec.force_halide_polynomial > 0 ? ("polynomial-" + std::to_string(prec.force_halide_polynomial)).c_str() : "maybe-intrinsic",
                       em.max_abs_error, em.max_rel_error, em.max_ulp_error, em.max_mantissa_error,
                       em.mean_abs_error, em.mean_ulp_error);

                for (const ErrorMetrics::Worst &w : {em.worst_abs, em.worst_ulp}) {
                    printf(" (worst: (act)%+.8e != (exp)%+.8e @ %s",
                           w.actual,
                           w.expected,
                           ftt.name.c_str());
                    if (is_2d) {
                        printf("(%e, %e))", out_input_0(w.where), out_input_1(w.where));
                    } else {
                        printf("(%e))", out_input_0(w.where));
                    }
                }

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
                    if (ftt.obtain_approximation && test.precision.force_halide_polynomial > 0 &&
                        (!rat.requires_strict_float || target_has_proper_strict_float_support)) {
                        // We have tabular data indicating expected precision.
                        const Halide::Internal::Approximation *approx = ftt.obtain_approximation(prec, arg_x.type());
                        const Halide::Internal::Approximation::Metrics &metrics = approx->metrics_for(arg_x.type());
                        rat.max_ulp.eval("MaxUlp", metrics.mulpe, em.max_ulp_error, num_tests, num_tests_passed);
                        rat.mean_ulp.eval("MeanUlp", metrics.mulpe, em.mean_ulp_error, num_tests, num_tests_passed);
                        rat.max_abs.eval("MaxAbs", metrics.mae, em.max_abs_error, num_tests, num_tests_passed);
                        rat.mean_abs.eval("MeanAbs", metrics.mae, em.mean_abs_error, num_tests, num_tests_passed);
                    }

                    {
                        // If we don't validate the MAE strictly, let's check if at least it gives
                        // reasonable results when the MAE <= 1e-5 is desired.
                        if (prec.constraint_max_absolute_error != 0 &&
                            prec.constraint_max_absolute_error <= 1e-5) {
                            num_tests++;
                            if (em.mean_abs_error < 1e-5 ||
                                em.mean_ulp_error < 20'000 ||
                                em.mean_rel_error < 1e-2) {
                                num_tests_passed++;
                                print_ok();
                            } else {
                                print_bad("Not precise on average in any way!");
                            }
                        }
                    }
                }

                if (prec.constraint_max_absolute_error != 0 &&
                    prec.constraint_max_absolute_error <= 1e-5 &&
                    prec.optimized_for == ApproximationPrecision::MULPE &&
                    (!rat.requires_strict_float || target_has_proper_strict_float_support)) {
                    if (rat.max_max_ulp_error != 0) {
                        num_tests++;
                        if (em.max_ulp_error > rat.max_max_ulp_error) {
                            print_bad("Max ULP");
                        } else {
                            print_ok();
                            num_tests_passed++;
                        }
                    }
                    if (rat.max_mean_ulp_error != 0) {
                        num_tests++;
                        if (em.mean_ulp_error > rat.max_mean_ulp_error) {
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
