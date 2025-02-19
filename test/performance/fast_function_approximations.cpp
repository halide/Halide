#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

struct FunctionToTest {
    std::string name;
    float lower_x, upper_x;
    float lower_y, upper_y;
    float lower_z, upper_z;
    std::function<Expr(Expr x, Expr y, Expr z)> make_reference;
    std::function<Expr(Expr x, Expr y, Expr z, Halide::ApproximationPrecision)> make_approximation;
    std::vector<Target::Feature> force_poly_not_faster_on{};
};

struct PrecisionToTest {
    ApproximationPrecision precision;
    const char *name;
} precisions_to_test[] = {
    {{}, "AUTO"},

    {ApproximationPrecision::poly_mae(2), "Poly2"},
    {ApproximationPrecision::poly_mae(3), "Poly3"},
    {ApproximationPrecision::poly_mae(4), "Poly4"},
    {ApproximationPrecision::poly_mae(5), "Poly5"},
    {ApproximationPrecision::poly_mae(6), "Poly6"},
    {ApproximationPrecision::poly_mae(7), "Poly7"},
    {ApproximationPrecision::poly_mae(8), "Poly8"},

    {ApproximationPrecision::max_abs_error(1e-2), "MAE 1e-2"},
    {ApproximationPrecision::max_abs_error(1e-3), "MAE 1e-3"},
    {ApproximationPrecision::max_abs_error(1e-4), "MAE 1e-4"},
    {ApproximationPrecision::max_abs_error(1e-5), "MAE 1e-5"},
    {ApproximationPrecision::max_abs_error(1e-6), "MAE 1e-6"},
    {ApproximationPrecision::max_abs_error(1e-7), "MAE 1e-7"},
    {ApproximationPrecision::max_abs_error(1e-8), "MAE 1e-8"},
};

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Var x{"x"}, y{"y"};
    Var xo{"xo"}, yo{"yo"}, xi{"xi"}, yi{"yi"};
    const int test_w = 512;
    const int test_h = 256;

    const int PRIME_0 = 73;
    const int PRIME_1 = 233;
    const int PRIME_2 = 661;

    Expr t0 = ((x * PRIME_0) % test_w) / float(test_w);
    Expr t1 = ((y * PRIME_1) % test_h) / float(test_h);
    // To make sure we time mostly the computation of the math function, and not
    // memory bandwidth, we will compute many evaluations of the function per output
    // and sum them. In my testing, GPUs suffer more from bandwith with this test,
    // so we give it even more function evaluations to compute per output.
    const int test_d = target.has_gpu_feature() ? 2048 : 128;
    RDom rdom{0, test_d};
    Expr t2 = ((rdom % PRIME_2) % test_d) / float(test_d);

    const double pipeline_time_to_ns_per_evaluation = 1e9 / double(test_w * test_h * test_d);
    const float range = 10.0f;
    const float pi = 3.141592f;

    int num_passed = 0;
    int num_tests = 0;

    // clang-format off
    FunctionToTest funcs[] = {
        {
            "tan",
            -range, range,
            0, 0,
            -1.0, 1.0,
            [](Expr x, Expr y, Expr z) { return Halide::tan(x + z); },
            [](Expr x, Expr y, Expr z, Halide::ApproximationPrecision prec) { return Halide::fast_tan(x + z, prec); },
            {Target::Feature::WebGPU, Target::Feature::Metal},
        },
        {
            "atan",
            -range, range,
            0, 0,
            -1.0, 1.0,
            [](Expr x, Expr y, Expr z) { return Halide::atan(x + z); },
            [](Expr x, Expr y, Expr z, Halide::ApproximationPrecision prec) { return Halide::fast_atan(x + z, prec); },
            {Target::Feature::WebGPU, Target::Feature::Metal},
        },
        {
            "atan2",
            -range, range,
            -range, range,
            -pi, pi,
            [](Expr x, Expr y, Expr z) { return Halide::atan2(x, y + z); },
            [](Expr x, Expr y, Expr z, Halide::ApproximationPrecision prec) { return Halide::fast_atan2(x, y + z, prec); },
            {Target::Feature::WebGPU, Target::Feature::Metal},
        },
        {
            "sin",
            -range, range,
            0, 0,
            -pi, pi,
            [](Expr x, Expr y, Expr z) { return Halide::sin(x + z); },
            [](Expr x, Expr y, Expr z, Halide::ApproximationPrecision prec) { return Halide::fast_sin(x + z, prec); },
            {Target::Feature::WebGPU, Target::Feature::Metal, Target::Feature::Vulkan},
        },
        {
            "cos",
            -range, range,
            0, 0,
            -pi, pi,
            [](Expr x, Expr y, Expr z) { return Halide::cos(x + z); },
            [](Expr x, Expr y, Expr z, Halide::ApproximationPrecision prec) { return Halide::fast_cos(x + z, prec); },
            {Target::Feature::WebGPU, Target::Feature::Metal, Target::Feature::Vulkan},
        },
        {
            "exp",
            -range, range,
            0, 0,
            -pi, pi,
            [](Expr x, Expr y, Expr z) { return Halide::exp(x + z); },
            [](Expr x, Expr y, Expr z, Halide::ApproximationPrecision prec) { return Halide::fast_exp(x + z, prec); },
            {Target::Feature::WebGPU, Target::Feature::Metal, Target::Feature::Vulkan, Target::Feature::OpenCL},
        },
        {
            "log",
            1e-8, range,
            0, 0,
            0, 1e-5,
            [](Expr x, Expr y, Expr z) { return Halide::log(x + z); },
            [](Expr x, Expr y, Expr z, Halide::ApproximationPrecision prec) { return Halide::fast_log(x + z, prec); },
            {Target::Feature::WebGPU, Target::Feature::Metal, Target::Feature::Vulkan},
        },
        {
            "pow",
            1e-8, range,
            -10, 10,
            0, 1e-5,
            [](Expr x, Expr y, Expr z) { return Halide::pow(x + z, y); },
            [](Expr x, Expr y, Expr z, Halide::ApproximationPrecision prec) { return Halide::fast_pow(x + z, y, prec); },
            {Target::Feature::WebGPU, Target::Feature::Metal, Target::Feature::Vulkan},
        },
        {
            "tanh",
            -10, 10,
            0, 0,
            -10, 10,
            [](Expr x, Expr y, Expr z) { return Halide::tanh(x + z); },
            [](Expr x, Expr y, Expr z, Halide::ApproximationPrecision prec) { return Halide::fast_tanh(x + z, prec); },
            {Target::Feature::CUDA, Target::Feature::Vulkan},
        },
    };
    // clang-format on

    std::function<void(Func &)> schedule = [&](Func &f) {
        if (target.has_gpu_feature()) {
            f.never_partition_all();
            f.gpu_tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::ShiftInwards);
        } else {
            f.vectorize(x, 8);
        }
    };
    Buffer<float> buffer_out(test_w, test_h);
    Halide::Tools::BenchmarkConfig bcfg;
    bcfg.max_time = 0.5;
    bcfg.min_time = 0.2;
    bcfg.accuracy = 0.015;
    for (FunctionToTest ftt : funcs) {
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

        Expr arg_x = strict_float(ftt.lower_x * (1.0f - t0) + ftt.upper_x * t0);
        Expr arg_y = strict_float(ftt.lower_y * (1.0f - t1) + ftt.upper_y * t1);
        Expr arg_z = strict_float(ftt.lower_z * (1.0f - t2) + ftt.upper_z * t2);

        // Reference function
        Func ref_func{ftt.name + "_ref"};
        ref_func(x, y) = sum(ftt.make_reference(arg_x, arg_y, arg_z));
        schedule(ref_func);
        ref_func.compile_jit();
        double pipeline_time_ref = benchmark([&]() { ref_func.realize(buffer_out); buffer_out.device_sync(); }, bcfg);

        // Print results for this function
        printf("      %s           : %9.5f ns per evaluation  [per invokation: %6.3f ms]\n",
               ftt.name.c_str(),
               pipeline_time_ref * pipeline_time_to_ns_per_evaluation,
               pipeline_time_ref * 1e3);

        for (PrecisionToTest &precision : precisions_to_test) {
            printf(" fast_%s (%8s):", ftt.name.c_str(), precision.name);

            Func approx_func{ftt.name + "_approx"};
            approx_func(x, y) = sum(ftt.make_approximation(arg_x, arg_y, arg_z, precision.precision));
            schedule(approx_func);
            approx_func.compile_jit();
            // clang-format off
            double approx_pipeline_time = benchmark([&]() {
                approx_func.realize(buffer_out);
                buffer_out.device_sync();
            }, bcfg);
            // clang-format on

            // Print results for this approximation.
            printf(" %9.5f ns per evaluation  (per invokation: %6.3f ms)",
                   approx_pipeline_time * pipeline_time_to_ns_per_evaluation,
                   approx_pipeline_time * 1e3);

            // Check for speedup
            bool should_be_faster = true;
            if (precision.precision.force_halide_polynomial != 0) {
                for (Target::Feature f : ftt.force_poly_not_faster_on) {
                    if (target.has_feature(f)) {
                        should_be_faster = false;
                    }
                }
            }
            if (should_be_faster) num_tests++;

            int goodness = 0;

            if (pipeline_time_ref < approx_pipeline_time * 0.90) {
                printf("   %6.1f%% slower", -100.0f * (1.0f - approx_pipeline_time / pipeline_time_ref));
                if (!should_be_faster) {
                    printf("  (expected)");
                    goodness = 1;
                } else {
                    printf("!!");
                    goodness = 0;
                }
            } else if (pipeline_time_ref < approx_pipeline_time * 1.10) {
                printf("   equally fast (%+5.1f%% faster)",
                       100.0f * (1.0f - approx_pipeline_time / pipeline_time_ref));
                if (should_be_faster) num_passed++;
                goodness = 1;
            } else {
                printf("   %4.1f%% faster",
                       100.0f * (1.0f - approx_pipeline_time / pipeline_time_ref));
                if (should_be_faster) num_passed++;
                goodness = 2;
            }

            switch (goodness) {
            case 0:
                printf(" ❌");
                break;
            case 1:
                printf(" 😐");
                break;
            case 2:
                printf(" ✅");
                break;
            }
            printf("\n");
        }
        printf("\n");
    }

    printf("Passed %d / %d performance test.\n", num_passed, num_tests);
    if (num_passed < num_tests) {
        printf("Not all measurements were faster (or equally fast) for the fast variants of the functions.\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
