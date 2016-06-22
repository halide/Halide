#include "Halide.h"
#include "benchmark.h"
#include <stdio.h>
using namespace Halide;

Expr sum3x3(Func f, Var x, Var y) {
    return f(x-1, y-1) + f(x-1, y) + f(x-1, y+1) +
            f(x, y-1)   + f(x, y)   + f(x, y+1) +
            f(x+1, y-1) + f(x+1, y) + f(x+1, y+1);
}

double run_test(bool auto_schedule) {
    int H = 1920;
    int W = 1024;
    Image<float> in(H, W, 3);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            for (int c = 0; c < 3; c++) {
                in(x, y, c) = rand() & 0xfff;
            }
        }
    }

    Func in_b = BoundaryConditions::repeat_edge(in);

    Var x, y, c;

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

    shifted.estimate(x, 0, 1920).estimate(y, 0, 1024);

    Target target = get_target_from_environment();
    Pipeline p(shifted);

    if (!auto_schedule) {
        Var yi, xi;
        if (target.has_gpu_feature()) {
            shifted.gpu_tile(x, y, 14, 14);
            Ix.compute_at(shifted, Var::gpu_blocks()).gpu_threads(x, y);
            Iy.compute_at(shifted, Var::gpu_blocks()).gpu_threads(x, y);
        } else {
            shifted.tile(x, y, xi, yi, 128, 128)
                    .vectorize(xi, 8).parallel(y);
            Ix.compute_at(shifted, x).vectorize(x, 8);
            Iy.compute_at(shifted, x).vectorize(x, 8);
        }
    } else {
        // Auto schedule the pipeline
        p.auto_schedule(target);
    }

    // Inspect the schedule
    shifted.print_loop_nest();

    // Run the schedule
    Image<float> out(1920, 1024);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
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
    printf("Success!\n");
    return 0;
}
