#include "Halide.h"
#include "get_autoscheduler_params.hpp"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

double run_test_1(bool auto_schedule) {
    Var x("x"), y("y"), dx("dx"), dy("dy"), c("c");

    int W = 1024;
    int H = 1920;
    int search_area = 7;

    Buffer<uint32_t> im(2048);
    im.fill(17);

    Func f("f");
    f(x, y, dx, dy) = im(x) + im(y + 1) + im(dx + search_area / 2) + im(dy + search_area / 2);

    RDom dom(-search_area / 2, search_area, -search_area / 2, search_area, "dom");

    // If 'f' is inlined into 'r', the only storage layout that the auto scheduler
    // needs to care about is that of 'r'.
    Func r("r");
    r(x, y, c) += f(x, y + 1, dom.x, dom.y) * f(x, y - 1, dom.x, dom.y) * c;

    Target target = get_jit_target_from_environment();
    Pipeline p(r);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        r.set_estimates({{0, W}, {0, H}, {0, 3}});
        // Auto-schedule the pipeline
        p.apply_autoscheduler(target, get_mullapudi2016_test_params(target.has_gpu_feature()));
    } else {
        Var par;
        r.update(0).fuse(c, y, par).parallel(par).reorder(x, dom.x, dom.y).vectorize(x, 4);
        r.fuse(c, y, par).parallel(par).vectorize(x, 4);
    }

    // Inspect the schedule (only for debugging))
    // r.print_loop_nest();

    // Run the schedule
    Buffer<int> out(W, H, 3);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t * 1000;
}

double run_test_2(bool auto_schedule) {
    Var x("x"), y("y"), z("z"), c("c");

    int W = 1024;
    int H = 1920;
    Buffer<uint8_t> left_im(W, H, 3);
    Buffer<uint8_t> right_im(W, H, 3);

    for (int y = 0; y < left_im.height(); y++) {
        for (int x = 0; x < left_im.width(); x++) {
            for (int c = 0; c < 3; c++) {
                left_im(x, y, c) = rand() & 0xfff;
                right_im(x, y, c) = rand() & 0xfff;
            }
        }
    }

    Func left = BoundaryConditions::repeat_edge(left_im);
    Func right = BoundaryConditions::repeat_edge(right_im);

    Func diff;
    diff(x, y, z, c) = min(absd(left(x, y, c), right(x + 2 * z, y, c)),
                           absd(left(x, y, c), right(x + 2 * z + 1, y, c)));

    Target target = get_jit_target_from_environment();
    Pipeline p(diff);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        diff.set_estimates({{0, left_im.width()}, {0, left_im.height()}, {0, 32}, {0, 3}});

        // Auto-schedule the pipeline
        //
        // Increasing the GPU's active warp count estimate (aka parallelism)
        // from 128 to 2048 to disable the Autoscheduler's grid-stride loop
        // feature. At small parallelism value, the autoscheduler correctly
        // designates dimension 'z' as the stride axis in the GPU grid-stride
        // loop, which improves thread occupancy. However, it fails to reorder
        // 'z' inside the gpu_blocks 'xo' and 'yo', which is required for proper
        // loop nesting and successful code generation.
        //
        // Reference:
        // https://developer.nvidia.com/blog/cuda-pro-tip-write-flexible-kernels-grid-stride-loops/
        constexpr Mullapudi2016TestParams gpu_specifications{
            /* .last_level_cache_size = */ 47'000,
            /* .parallelism = */ 2048,
        };

        p.apply_autoscheduler(
            target, get_mullapudi2016_test_params(target.has_gpu_feature(), {gpu_specifications}));
    } else {
        Var t("t");
        diff.reorder(c, z).fuse(c, z, t).parallel(t).vectorize(x, 16);
    }

    // Inspect the schedule (only for debugging))
    // diff.print_loop_nest();

    // Run the schedule
    Buffer<uint8_t> out(left_im.width(), left_im.height(), 32, 3);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t * 1000;
}

double run_test_3(bool auto_schedule) {
    Buffer<uint8_t> im(1024, 1028, 14, 14);

    Var x("x"), y("y"), dx("dx"), dy("dy"), c("c");

    Func f("f");
    f(x, y, dx, dy) = im(x, y, dx, dy);

    int search_area = 7;
    RDom dom(-search_area / 2, search_area, -search_area / 2, search_area, "dom");

    Func r("r");
    r(x, y, c) += f(x, y + 1, search_area / 2 + dom.x, search_area / 2 + dom.y) *
                  f(x, y + 2, search_area / 2 + dom.x, search_area / 2 + dom.y) * c;

    Target target = get_jit_target_from_environment();
    Pipeline p(r);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        r.set_estimates({{0, 1024}, {0, 1024}, {0, 3}});
        // Auto-schedule the pipeline
        //
        // Disabling this experimental GPU feature because the autoscheduler correctly
        // identifies reduction domain 'r.x' as the stride axis for the GPU grid-stride loop,
        // which helps retain threads efficiently. However, it fails to reorder 'r.x'
        // inside the loop nests of gpu_blocks 'xo' and 'yo', which is necessary for
        // successful code generation.
        //
        // Reference: https://developer.nvidia.com/blog/cuda-pro-tip-write-flexible-kernels-grid-stride-loops/
        p.apply_autoscheduler(target, get_mullapudi2016_test_params(target.has_gpu_feature()));
    } else {
        Var par("par");
        r.update(0).fuse(c, y, par).parallel(par).reorder(x, dom.x, dom.y).vectorize(x, 4);
        r.fuse(c, y, par).parallel(par).vectorize(x, 4);
    }

    // Inspect the schedule (only for debugging))
    // r.print_loop_nest();

    // Run the schedule
    Buffer<int> out(1024, 1024, 3);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t * 1000;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Autoschedulers do not support WebAssembly.\n");
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }

    load_plugin(argv[1]);

    {
        double manual_time = run_test_1(false);
        double auto_time = run_test_1(true);

        const double slowdown_factor = 2.0;
        if (!get_jit_target_from_environment().has_gpu_feature() && auto_time > manual_time * slowdown_factor) {
            std::cerr << "Autoscheduler time (1) is slower than expected:\n"
                      << "======================\n"
                      << "Manual time: " << manual_time << "ms\n"
                      << "Auto time: " << auto_time << "ms\n"
                      << "======================\n";
            exit(1);
        }
    }

    {
        double manual_time = run_test_2(false);
        double auto_time = run_test_2(true);

        const double slowdown_factor = 2.0;
        if (!get_jit_target_from_environment().has_gpu_feature() && auto_time > manual_time * slowdown_factor) {
            std::cerr << "Autoscheduler time (2) is slower than expected:\n"
                      << "======================\n"
                      << "Manual time: " << manual_time << "ms\n"
                      << "Auto time: " << auto_time << "ms\n"
                      << "======================\n";
            exit(1);
        }
    }

    if (get_jit_target_from_environment().has_gpu_feature()) {
        std::cout << "Mullapudi for GPU test for Test Case 3 skipped because of reordering bug.\n";
    } else {

        double manual_time = run_test_3(false);
        double auto_time = run_test_3(true);

        const double slowdown_factor = 2.0;
        if (!get_jit_target_from_environment().has_gpu_feature() && auto_time > manual_time * slowdown_factor) {
            std::cerr << "Autoscheduler time (3) is slower than expected:\n"
                      << "======================\n"
                      << "Manual time: " << manual_time << "ms\n"
                      << "Auto time: " << auto_time << "ms\n"
                      << "======================\n";
            exit(1);
        }
    }

    printf("Success!\n");
    return 0;
}
