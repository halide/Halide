#include "Halide.h"
#include "halide_benchmark.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Tools;

Expr sum3x3(Func f, Var x, Var y) {
    return f(x-1, y-1) + f(x-1, y) + f(x-1, y+1) +
           f(x, y-1)   + f(x, y)   + f(x, y+1) +
           f(x+1, y-1) + f(x+1, y) + f(x+1, y+1);
}

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

    Func in_b = BoundaryConditions::repeat_edge(in);

    Var x("x"), y("y"), c("c");

    Func gray("gray");
    gray(x, y) = 0.299f * in_b(x, y, 0) + 0.587f * in_b(x, y, 1) + 0.114f * in_b(x, y, 2);

    Func Iy("Iy");
    Iy(x, y) = gray(x-1, y-1)*(-1.0f/12) + gray(x-1, y+1)*(1.0f/12) +
               gray(x, y-1)*(-2.0f/12) + gray(x, y+1)*(2.0f/12) +
               gray(x+1, y-1)*(-1.0f/12) + gray(x+1, y+1)*(1.0f/12);

    Func Ix("Ix");
    Ix(x, y) = gray(x-1, y-1)*(-1.0f/12) + gray(x+1, y-1)*(1.0f/12) +
               gray(x-1, y)*(-2.0f/12) + gray(x+1, y)*(2.0f/12) +
               gray(x-1, y+1)*(-1.0f/12) + gray(x+1, y+1)*(1.0f/12);

    Func Ixx("Ixx");
    Ixx(x, y) = Ix(x, y) * Ix(x, y);

    Func Iyy("Iyy");
    Iyy(x, y) = Iy(x, y) * Iy(x, y);

    Func Ixy("Ixy");
    Ixy(x, y) = Ix(x, y) * Iy(x, y);

    Func Sxx("Sxx");

    Sxx(x, y) = sum3x3(Ixx, x, y);

    Func Syy("Syy");
    Syy(x, y) = sum3x3(Iyy, x, y);

    Func Sxy("Sxy");
    Sxy(x, y) = sum3x3(Ixy, x, y);

    Func det("det");
    det(x, y) = Sxx(x, y) * Syy(x, y) - Sxy(x, y) * Sxy(x, y);

    Func trace("trace");
    trace(x, y) = Sxx(x, y) + Syy(x, y);

    Func harris("harris");
    harris(x, y) = det(x, y) - 0.04f * trace(x, y) * trace(x, y);

    Func shifted("shifted");
    shifted(x, y) = harris(x + 2, y + 2);

    shifted.estimate(x, 0, W).estimate(y, 0, H);

    Target target = get_jit_target_from_environment();
    Pipeline p(shifted);

    if (!auto_schedule) {
        Var xi("xi"), yi("yi");
        if (target.has_gpu_feature()) {
            shifted.gpu_tile(x, y, xi, yi, 14, 14);
            Ix.compute_at(shifted, x).gpu_threads(x, y);
            Iy.compute_at(shifted, x).gpu_threads(x, y);
        } else {
            shifted.tile(x, y, xi, yi, 128, 128)
                   .vectorize(xi, 8).parallel(y);
            Ix.compute_at(shifted, x).vectorize(x, 8);
            Iy.compute_at(shifted, x).vectorize(x, 8);
        }
    } else {
        // Auto-schedule the pipeline
        p.auto_schedule(target);
    }

    // Inspect the schedule
    shifted.print_loop_nest();

    // Run the schedule
    Buffer<float> out(W, H);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t*1000;
}

int main(int argc, char **argv) {
    const double slowdown_factor = 2.0;
    double manual_time = run_test(false);
    double auto_time = run_test(true);

    std::cout << "======================" << std::endl;
    std::cout << "Manual time: " << manual_time << "ms" << std::endl;
    std::cout << "Auto time: " << auto_time << "ms" << std::endl;
    std::cout << "======================" << std::endl;

    if (!get_jit_target_from_environment().has_gpu_feature() &&
        (auto_time > slowdown_factor * manual_time)) {
        printf("Auto-scheduler is much much slower than it should be.\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
