#include "Halide.h"

#include <cinttypes>
#include <locale.h>

using namespace Halide;
using namespace Halide::Internal;

constexpr double PI = 3.14159265358979323846;
constexpr double ONE_OVER_PI = 1.0 / PI;
constexpr double TWO_OVER_PI = 2.0 / PI;
constexpr double PI_OVER_TWO = PI / 2;
constexpr double PI_OVER_FOUR = PI / 4;

constexpr uint32_t f32_signbit_mask = 0x80000000;

Expr int_to_float(Expr i) {
    Expr ampl_i = i & (~f32_signbit_mask);
    Expr ampl_f = Halide::reinterpret(Float(32), ampl_i);
    return select(i < 0, -ampl_f, ampl_f);
}

Expr float_to_int(Expr f) {
    Expr i = Halide::reinterpret(UInt(32), f);
    Expr ampl_i = i & (~f32_signbit_mask);
    return select(f < 0, -ampl_i, ampl_i);
}

struct TestRange {
    float l, u;

    int32_t lower_int() const {
        uint32_t a = Halide::Internal::reinterpret_bits<uint32_t>(l);
        uint32_t b = a & (~f32_signbit_mask);
        return (a & f32_signbit_mask) ? (-int64_t(b)) : b;
    }

    int32_t upper_int() const {
        uint32_t a = Halide::Internal::reinterpret_bits<uint32_t>(u);
        uint32_t b = a & (~f32_signbit_mask);
        return (a & f32_signbit_mask) ? (-int64_t(b)) : b;
    }

    uint32_t num_floats() const {
        int32_t li = lower_int();
        int32_t ui = upper_int();
        assert(li <= ui);
        int64_t num = int64_t(ui) - int64_t(li) + 1;
        assert(num == uint32_t(num));
        return num;
    }
};

using OO = Halide::ApproximationPrecision::OptimizationObjective;

constexpr float just_not_pi_over_two = std::nexttoward(float(PI_OVER_TWO), 0.0f);

struct FunctionToTest {
    std::string name;
    OO oo;
    std::function<Expr(Expr x, Expr y)> make_reference;
    std::function<Expr(Expr x, Expr y, Halide::ApproximationPrecision)> make_approximation;
    const Halide::Internal::Approximation *(*obtain_approximation)(Halide::ApproximationPrecision, Halide::Type);
    const std::vector<Halide::Internal::Approximation> &table;
    TestRange range_x{0.0f, 0.0f};
    TestRange range_y{0.0f, 0.0f};
} functions_to_test[] = {
    // clang-format off
    {
        "tan", OO::MULPE,
        [](Expr x, Expr y) { return Halide::tan(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_tan(x, prec); },
        Halide::Internal::ApproximationTables::best_tan_approximation,
        Halide::Internal::ApproximationTables::table_tan,
        {0.0f, float(PI_OVER_FOUR)},
    },
    {
        "atan", OO::MULPE,
        [](Expr x, Expr y) { return Halide::atan(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_atan(x, prec); },
        Halide::Internal::ApproximationTables::best_atan_approximation,
        Halide::Internal::ApproximationTables::table_atan,
        {0.0f, 32.0f},
    },
    {
        "sin", OO::MULPE,
        [](Expr x, Expr y) { return Halide::sin(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_sin(x, prec); },
        Halide::Internal::ApproximationTables::best_sin_approximation,
        Halide::Internal::ApproximationTables::table_sin,
        {0.0f, PI_OVER_TWO},
    },
    {
        "cos", OO::MAE, // Only MAE uses the cos table. MULPE gets redirected to fast_sin.
        [](Expr x, Expr y) { return Halide::cos(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_cos(x, prec); },
        Halide::Internal::ApproximationTables::best_cos_approximation,
        Halide::Internal::ApproximationTables::table_cos,
        {0.0f, PI_OVER_TWO},
    },
    {
        "exp", OO::MULPE,
        [](Expr x, Expr y) { return Halide::exp(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_exp(x, prec); },
        Halide::Internal::ApproximationTables::best_exp_approximation,
        Halide::Internal::ApproximationTables::table_exp,
        {0.0f, std::log(2.0)},
    },
    {
        "log", OO::MULPE,
        [](Expr x, Expr y) { return Halide::log(x); },
        [](Expr x, Expr y, Halide::ApproximationPrecision prec) { return Halide::fast_log(x, prec); },
        Halide::Internal::ApproximationTables::best_log_approximation,
        Halide::Internal::ApproximationTables::table_log,
        {0.75f, 1.50f},
    },
    // clang-format on
};

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch != Halide::Target::X86) {
        printf("[SKIP] Please run this on x86 such that we can disable FMA.");
        return 0;
    }
    setlocale(LC_NUMERIC, "");

    Target target_no_fma;
    target_no_fma.os = target.os;
    target_no_fma.arch = target.arch;
    target_no_fma.bits = target.bits;
    target_no_fma.vector_bits = target.vector_bits;

    auto out_mae = Buffer<float>::make_scalar();
    auto out_mulpe = Buffer<int>::make_scalar();
    auto out_mae_fma = Buffer<float>::make_scalar();
    auto out_mulpe_fma = Buffer<int>::make_scalar();

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

        TestRange range_x = ftt.range_x;
        TestRange range_y = ftt.range_y;

        const int num_floats_x = range_x.num_floats();
        const int num_floats_y = range_y.num_floats();
        printf("\n📏 Testing fast_%s on range ([%f, %f] x [%f, %f]) = %d x %d floats...\n", ftt.name.c_str(),
               range_x.l, range_x.u, range_y.l, range_y.u, num_floats_x, num_floats_y);
        RDom r({{0, num_floats_x}, {0, num_floats_y}}, "rdom");

        Halide::Type type = Float(32);

        // Approximations:
        int table_entry_idx = 0;
        for (const Halide::Internal::Approximation &approx : ftt.table) {
            Approximation::Metrics metrics = approx.metrics_for(type);
            Halide::ApproximationPrecision prec;
            prec.optimized_for = ftt.oo;
            prec.force_halide_polynomial = (table_entry_idx++) | (1 << 31);  // Special code to request a particular entry by index.

            const Halide::Internal::Approximation *selected_approx = ftt.obtain_approximation(prec, type);
            if (selected_approx != &approx) {
                auto &sel = *selected_approx;
                printf("Approximation selection algorithm did not select approximation we expected!\n");
                printf("Requested: p=%zu, q=%zu, mae=%.5e, mulpe=%" PRIu64 "\n", approx.p.size(), approx.q.size(), approx.metrics_f32.mae, approx.metrics_f32.mulpe);
                printf("Received : p=%zu, q=%zu, mae=%.5e, mulpe=%" PRIu64 "\n", sel.p.size(), sel.q.size(), sel.metrics_f32.mae, sel.metrics_f32.mulpe);
                abort();
            }

            std::string name = ftt.name + "_approx";
            if (approx.q.empty()) {
                name += "_poly" + std::to_string(approx.p.size());
            } else {
                name += "_pade_" + std::to_string(approx.p.size()) + "_" + std::to_string(approx.q.size());
            }

            Var x{"x"}, y{"y"};
            Func input_x{"input_x"}, input_y{"input_y"};
            input_x(x) = int_to_float(x + range_x.lower_int());
            input_y(y) = int_to_float(y + range_y.lower_int());

            // Reference function on CPU
            Func ref_func{ftt.name + "_ref_cpu_via_double"};
            ref_func(x, y) = cast<float>(ftt.make_reference(cast<double>(input_x(x)), cast<double>(input_y(y))));
            // No schedule: scalar evaluation using libm calls on CPU.

            Func approx_func{name};
            approx_func(x, y) = ftt.make_approximation(input_x(x), input_y(y), prec);

            Func error{"error"};
            error(x, y) = {
                Halide::absd(approx_func(x, y), ref_func(x, y)),
                Halide::absd(float_to_int(approx_func(x, y)), float_to_int(ref_func(x, y))),
            };

            Func max_error{"max_error"};
            max_error() = {0.0f, 0};
            max_error() = {
                max(max_error()[0], error(r.x, r.y)[0]),
                max(max_error()[1], error(r.x, r.y)[1]),
            };

            RVar rxo{"rxo"}, rxi{"rxi"};
            Var block{"block"};
            max_error.never_partition_all();
            Func intm = max_error.update()
                            .split(r.x, rxo, rxi, 1 << 16)
                            .rfactor(rxo, block)
                            .never_partition_all();
            intm.compute_root();
            intm.update().vectorize(block, 8).parallel(block).never_partition_all();  //.atomic().vectorize(rxi, 8);

            input_x.never_partition_all().compute_at(intm, rxi);
            input_y.never_partition_all().compute_at(intm, rxi);
            ref_func.compute_at(intm, rxi).never_partition_all();
            approx_func.compute_at(intm, rxi).never_partition_all();

            max_error.update().never_partition_all().atomic().vectorize(rxo, 16);
            max_error.realize({out_mae, out_mulpe}, target_no_fma);

            // Reconstruct printing the FULL table entry.
            constexpr auto printc = [](double c) {
                if (c == 0.0) {
                    printf("0");
                } else if (c == 1.0) {
                    printf("1");
                } else {
                    printf("%a", c);
                }
            };
            constexpr auto print_poly = [](const std::vector<double> &coef) {
                bool printed = false;
                for (size_t i = 0; i < coef.size(); ++i) {
                    double c = coef[i];
                    if (c != 0.0) {
                        if (printed) {
                            printf(" + ");
                        }
                        printed = true;
                        if (c == 1) {
                            printf("1");
                        } else {
                            printf("%.13f", coef[i]);
                        }
                        if (i > 0) {
                            printf("*x");
                            if (i > 1) {
                                printf("^%zu", i);
                            }
                        }
                    }
                }
            };
            auto m16 = approx.metrics_f16;
            auto m64 = approx.metrics_f64;
            printf("{ /* ");
            if (approx.q.empty()) {
                printf("Polynomial degree %zu: ", approx.p.size() - 1);
                print_poly(approx.p);
            } else {
                printf("Padé approximant %zu/%zu: (", approx.p.size() - 1, approx.q.size() - 1);
                print_poly(approx.p);
                printf(")/(");
                print_poly(approx.q);
                printf(")");
            }
            printf(" */\n");
            printf("    /* f16 */ {%.6e, %a, %" PRIu64 "},\n", m16.mse, m16.mae, m16.mulpe);
            printf("    /* f32 */ {%.6e, %a, %" PRIu64 "},\n", metrics.mse, out_mae(), uint64_t(out_mulpe()));
            printf("    /* f64 */ {%.6e, %a, %" PRIu64 "},\n", m64.mse, m64.mae, m64.mulpe);
            printf("    /* p */ {");
            const char *sep = "";
            for (double c : approx.p) {
                printf("%s", sep);
                printc(c);
                sep = ", ";
            }
            printf("},\n");
            if (!approx.q.empty()) {
                printf("    /* q */ {");
                sep = "";
                for (double c : approx.q) {
                    printf("%s", sep);
                    printc(c);
                    sep = ", ";
                }
                printf("},\n");
            }
            printf("},\n");
        }
    }
    printf("Success!\n");
    return 0;
}
