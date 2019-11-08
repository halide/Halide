#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

const float PI = 3.14159265358979323846f;

double run_test(bool auto_schedule) {
    int W = 1920;
    int H = 1024;
    Buffer<float> in(W, H, 3);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            for (int c = 0; c < 3; c++) {
                in(x, y, c) = rand() & 0xfff;
            }
        }
    }

    // Define a 7x7 Gaussian Blur with a repeat-edge boundary condition.
    float sigma = 1.5f;

    Var x, y, c;
    Func kernel("kernel");
    kernel(x) = exp(-x*x/(2*sigma*sigma)) / (sqrtf(2*PI)*sigma);

    Func in_bounded = BoundaryConditions::repeat_edge(in);

    Func gray("gray");
    gray(x, y) = 0.299f * in_bounded(x, y, 0) + 0.587f * in_bounded(x, y, 1) +
                 0.114f * in_bounded(x, y, 2);

    Func blur_y("blur_y");
    blur_y(x, y) = (kernel(0) * gray(x, y) +
                    kernel(1) * (gray(x, y-1) + gray(x, y+1)) +
                    kernel(2) * (gray(x, y-2) + gray(x, y+2)) +
                    kernel(3) * (gray(x, y-3) + gray(x, y+3)));

    Func blur_x("blur_x");
    blur_x(x, y) = (kernel(0) * blur_y(x, y) +
                    kernel(1) * (blur_y(x-1, y) + blur_y(x+1, y)) +
                    kernel(2) * (blur_y(x-2, y) + blur_y(x+2, y)) +
                    kernel(3) * (blur_y(x-3, y) + blur_y(x+3, y)));

    Func sharpen("sharpen");
    sharpen(x, y) = 2 * gray(x, y) - blur_x(x, y);

    Func ratio("ratio");
    ratio(x, y) = sharpen(x, y) / gray(x, y);

    Func unsharp("unsharp");
    unsharp(x, y, c) = ratio(x, y) * in(x, y, c);

    Target target = get_jit_target_from_environment();
    Pipeline p(unsharp);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        unsharp.estimate(x, 0, in.width())
            .estimate(y, 0, in.height())
            .estimate(c, 0, in.channels());
        // Auto-schedule the pipeline
        p.auto_schedule(target);
    } else if (target.has_gpu_feature()) {
        Var xi, yi;
        unsharp.compute_root()
            .reorder(c, x, y)
            .gpu_tile(x, y, xi, yi, 16, 16);
        ratio.compute_at(unsharp, xi);
        gray.compute_at(unsharp, x)
            .tile(x, y, xi, yi, 2, 2)
            .unroll(xi)
            .unroll(yi)
            .gpu_threads(x, y);
        blur_y.compute_at(unsharp, x)
            .unroll(x, 2)
            .gpu_threads(x, y);
    } else {
        blur_y.compute_at(unsharp, y).vectorize(x, 8);
        ratio.compute_at(unsharp, y).vectorize(x, 8);
        unsharp.vectorize(x, 8).parallel(y).reorder(x, c, y);
    }

    // Inspect the schedule
    unsharp.print_loop_nest();

    // Benchmark the schedule
    Buffer<float> out(in.width(), in.height(), in.channels());
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
        out.device_sync();
    });

    return t*1000;
}

int main(int argc, char **argv) {
    double manual_time = run_test(false);
    double auto_time = run_test(true);

    std::cout << "======================" << std::endl;
    std::cout << "Manual time: " << manual_time << "ms" << std::endl;
    std::cout << "Auto time: " << auto_time << "ms" << std::endl;
    std::cout << "======================" << std::endl;

    if (!get_jit_target_from_environment().has_gpu_feature() &&
        (auto_time > manual_time * 3)) {
        printf("Auto-scheduler is much much slower than it should be.\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
