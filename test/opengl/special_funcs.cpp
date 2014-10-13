#include <Halide.h>
#include <stdio.h>
#include <algorithm>
#include <stdlib.h>
#include <iostream>

using namespace Halide;

Var x, y, c;

double square(double x) {
    return x * x;
}

template <typename T>
void test_function(Expr e, Image<T> &cpu_result, Image<T> &gpu_result) {
    Func cpu, gpu;

    Target cpu_target = get_host_target();
    Target gpu_target = get_host_target();
    gpu_target.set_feature(Target::OpenGL);
    cpu(x, y, c) = e;
    gpu(x, y, c) = e;
    cpu.compile_jit(cpu_target);
    gpu.compile_jit(gpu_target);

    cpu.realize(cpu_result);

    gpu.bound(c, 0, 3).glsl(x, y, c);
    gpu.realize(gpu_result);
    gpu_result.copy_to_host();

}

template <typename T>
bool test_exact(Expr r, Expr g, Expr b) {
    Expr e = cast<T>(select(c == 0, r, c == 1, g, b));
    const int W = 256, H = 256;
    Image<T> cpu_result(W, H, 3);
    Image<T> gpu_result(W, H, 3);
    test_function(e, cpu_result, gpu_result);

    double err = 0.0;
    for (int y=0; y<gpu_result.height(); y++) {
        for (int x=0; x<gpu_result.width(); x++) {
            if (!(gpu_result(x, y, 0) == cpu_result(x, y, 0) &&
                  gpu_result(x, y, 1) == cpu_result(x, y, 1) &&
                  gpu_result(x, y, 2) == cpu_result(x, y, 2))) {
                std::cerr << "Incorrect pixel for " << e << " at (" << x << ", " << y << ")\n"
                          << "  ("
                          << (int)gpu_result(x, y, 0) << ", "
                          << (int)gpu_result(x, y, 1) << ", "
                          << (int)gpu_result(x, y, 2) << ") != ("
                          << (int)cpu_result(x, y, 0) << ", "
                          << (int)cpu_result(x, y, 1) << ", "
                          << (int)cpu_result(x, y, 2)
                          << ")\n";
                return false;
            }
        }
    }
    return true;
}

template <typename T>
bool test_approx(Expr r, Expr g, Expr b, double rms_error) {
    Expr e = cast<T>(select(c == 0, r, c == 1, g, b));
    const int W = 256, H = 256;
    Image<T> cpu_result(W, H, 3);
    Image<T> gpu_result(W, H, 3);
    test_function(e, cpu_result, gpu_result);

    double err = 0.0;
    for (int y=0; y<gpu_result.height(); y++) {
        for (int x=0; x<gpu_result.width(); x++) {
            err += square(gpu_result(x, y, 0) - cpu_result(x, y, 0));
            err += square(gpu_result(x, y, 1) - cpu_result(x, y, 1));
            err += square(gpu_result(x, y, 2) - cpu_result(x, y, 2));
        }
    }
    err = sqrt(err / (W * H));
    if (err > rms_error) {
        std::cerr << "RMS error too large for " << e << ": "
                  << err << " > " << rms_error << "\n";
        return false;
    } else {
        return true;
    }
}

int main() {
    bool ok = true;
    double rms;

    ok = ok && test_exact<uint8_t>(0, 0, 0);
    ok = ok && test_exact<uint8_t>(clamp(x + y, 0, 255), 0, 0);

    ok = ok && test_exact<uint8_t>(
        max(x, y),
        cast<int>(min(cast<float>(x), cast<float>(y))),
        clamp(x, 0, 10));

    // Trigonometric functions in GLSL are fast but not very accurate,
    // especially outside of 0..2pi.
    Expr r = (256 * x + y) / ceilf(65536.f / (2 * 3.1415926536f));
    ok = test_approx<float>(sin(r), cos(r), 0, 5e-7);

    ok = ok && test_exact<uint8_t>(
        (x - 127) / 3 + 127,
        (x - 127) % 3 + 127,
        0);

    ok = ok && test_exact<uint8_t>(
        lerp(cast<uint8_t>(x), cast<uint8_t>(y), cast<uint8_t>(128)),
        lerp(cast<uint8_t>(x), cast<uint8_t>(y), 0.5f),
        cast<uint8_t>(lerp(cast<float>(x), cast<float>(y), 0.2f)));

    if (ok) {
        printf("Success!\n");
        return 0;
    } else {
        return 1;
    }
}
