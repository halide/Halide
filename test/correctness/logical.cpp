#include "Halide.h"
#include <stdio.h>

using namespace Halide;

Expr u8(Expr a) {
    return cast<uint8_t>(a);
}

Expr u16(Expr a) {
    return cast<uint16_t>(a);
}

int main(int argc, char **argv) {

    Buffer<uint8_t> input(128, 64);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = y * input.width() + x;
        }
    }

    Var x, y, xi, yi;
    {
        Func f;
        f(x, y) = select(((input(x, y) > 10) && (input(x, y) < 20)) ||
                             ((input(x, y) > 40) && (!(input(x, y) > 50))),
                         u8(255), u8(0));

        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, xi, yi, 16, 16);
            if (!target.has_feature(Target::OpenGLCompute)) {
                f.vectorize(xi, 4);
            }
        } else if (target.has_feature(Target::HVX)) {
            f.hexagon().vectorize(x, 128);
        } else {
            f.vectorize(x, 8);
        }

        Buffer<uint8_t> output = f.realize({input.width(), input.height()}, target);

        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                bool cond = ((input(x, y) > 10) && (input(x, y) < 20)) ||
                            ((input(x, y) > 40) && (!(input(x, y) > 50)));
                uint8_t correct = cond ? 255 : 0;
                if (correct != output(x, y)) {
                    fprintf(stderr, "output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                    return 1;
                }
            }
        }
    }

    // Test a condition that uses a let resulting from common
    // subexpression elimination.
    {
        Func f;
        Expr common_cond = input(x, y) > 10;
        f(x, y) = select((common_cond && (input(x, y) < 20)) ||
                             ((input(x, y) > 40) && (!common_cond)),
                         u8(255), u8(0));

        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, xi, yi, 16, 16);
            if (!target.has_feature(Target::OpenGLCompute)) {
                f.vectorize(xi, 4);
            }
        } else if (target.has_feature(Target::HVX)) {
            f.hexagon().vectorize(x, 128);
        } else {
            f.vectorize(x, 8);
        }

        Buffer<uint8_t> output = f.realize({input.width(), input.height()}, target);

        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                bool common_cond = input(x, y) > 10;
                bool cond = (common_cond && (input(x, y) < 20)) ||
                            ((input(x, y) > 40) && (!common_cond));
                uint8_t correct = cond ? 255 : 0;
                if (correct != output(x, y)) {
                    fprintf(stderr, "output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                    return 1;
                }
            }
        }
    }

    // Test a condition which has vector and scalar inputs.
    {
        Func f("f");
        f(x, y) = select(x < 10 || x > 20 || y < 10 || y > 20, 0, input(x, y));

        Target target = get_jit_target_from_environment();

        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, xi, yi, 16, 16);
            if (!target.has_feature(Target::OpenGLCompute)) {
                f.vectorize(xi, 4);
            }
        } else if (target.has_feature(Target::HVX)) {
            f.hexagon().vectorize(x, 128);
        } else {
            f.vectorize(x, 128);
        }

        Buffer<uint8_t> output = f.realize({input.width(), input.height()}, target);

        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                bool cond = x < 10 || x > 20 || y < 10 || y > 20;
                uint8_t correct = cond ? 0 : input(x, y);
                if (correct != output(x, y)) {
                    fprintf(stderr, "output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                    return 1;
                }
            }
        }
    }

    // Test a condition that uses differently sized types.
    {
        Func f;
        Expr ten = 10;
        f(x, y) = select(input(x, y) > ten, u8(255), u8(0));

        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, xi, yi, 16, 16);
            if (!target.has_feature(Target::OpenGLCompute)) {
                f.vectorize(xi, 4);
            }
        } else if (target.has_feature(Target::HVX)) {
            f.hexagon().vectorize(x, 128);
        } else {
            f.vectorize(x, 8);
        }

        Buffer<uint8_t> output = f.realize({input.width(), input.height()}, target);

        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                bool cond = input(x, y) > 10;
                uint8_t correct = cond ? 255 : 0;
                if (correct != output(x, y)) {
                    fprintf(stderr, "output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                    return 1;
                }
            }
        }
    }

    // Test a select where the condition has a different width than
    // the true/false values.
    for (int w = 8; w <= 32; w *= 2) {
        for (int n = 8; n < w; n *= 2) {
            Type narrow = UInt(n), wide = UInt(w);

            Func in_wide;
            in_wide(x, y) = cast(wide, y + x * 3);
            in_wide.compute_root();

            Func in_narrow;
            in_narrow(x, y) = cast(narrow, x * y + x - 17);
            in_narrow.compute_root();

            Func f;
            f(x, y) = select(in_narrow(x, y) > 10, in_wide(x, y * 2), in_wide(x, y * 2 + 1));

            Func cpu;
            cpu(x, y) = f(x, y);

            Func gpu;
            gpu(x, y) = f(x, y);

            Func out;
            out(x, y) = {cast<uint32_t>(cpu(x, y)), cast<uint32_t>(gpu(x, y))};

            cpu.compute_root();
            gpu.compute_root();

            Target target = get_jit_target_from_environment();
            if (target.has_feature(Target::OpenCL) && n == 16 && w == 32) {
                // Workaround for https://github.com/halide/Halide/issues/2477
                printf("Skipping uint%d -> uint%d for OpenCL\n", n, w);
                continue;
            }
            if (target.has_gpu_feature()) {
                gpu.gpu_tile(x, y, xi, yi, 16, 16);
                if (!target.has_feature(Target::OpenGLCompute)) {
                    gpu.vectorize(xi, 4);
                }
            } else if (target.has_feature(Target::HVX)) {
                gpu.hexagon().vectorize(x, 128);
            } else {
                // Just test vectorization
                gpu.vectorize(x, 8);
            }

            Realization r = out.realize({input.width(), input.height()}, target);
            Buffer<uint32_t> cpu_output = r[0];
            Buffer<uint32_t> gpu_output = r[1];

            for (int y = 0; y < input.height(); y++) {
                for (int x = 0; x < input.width(); x++) {
                    if (cpu_output(x, y) != gpu_output(x, y)) {
                        fprintf(stderr, "gpu_output(%d, %d) = %d instead of %d for uint%d -> uint%d\n",
                                x, y, gpu_output(x, y), cpu_output(x, y), n, w);
                        return 1;
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
