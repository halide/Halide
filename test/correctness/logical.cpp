#include <stdio.h>
#include "Halide.h"

using namespace Halide;

Expr u8(Expr a) {
    return cast<uint8_t>(a);
}

Expr u16(Expr a) {
    return cast<uint16_t>(a);
}

int main(int argc, char **argv) {

    Image<uint8_t> input(128, 64);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = y*input.width() + x;
        }
    }

    Var x, y;
    {
        Func f;
        f(x, y) = select(((input(x, y) > 10) && (input(x, y) < 20)) ||
                         ((input(x, y) > 40) && (!(input(x, y) > 50))),
                         u8(255), u8(0));

        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, 16, 16).vectorize(Var::gpu_threads(), 4);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            f.hexagon().vectorize(x, 128);
        } else {
            f.vectorize(x, 8);
        }

        Image<uint8_t> output = f.realize(input.width(), input.height(), target);

        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                bool cond = ((input(x, y) > 10) && (input(x, y) < 20)) ||
                    ((input(x, y) > 40) && (!(input(x, y) > 50)));
                uint8_t correct = cond ? 255 : 0;
                if (correct != output(x, y)) {
                    printf("output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                    return -1;
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
            f.gpu_tile(x, y, 16, 16).vectorize(Var::gpu_threads(), 4);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            f.hexagon().vectorize(x, 128);
        } else {
            f.vectorize(x, 8);
        }

        Image<uint8_t> output = f.realize(input.width(), input.height(), target);

        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                bool common_cond = input(x, y) > 10;
                bool cond = (common_cond && (input(x, y) < 20)) ||
                    ((input(x, y) > 40) && (!common_cond));
                uint8_t correct = cond ? 255 : 0;
                if (correct != output(x, y)) {
                    printf("output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                    return -1;
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
            f.gpu_tile(x, y, 16, 16).vectorize(Var::gpu_threads(), 4);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            f.hexagon().vectorize(x, 128);
        } else {
            f.vectorize(x, 8);
        }

        Image<uint8_t> output = f.realize(input.width(), input.height(), target);

        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                bool cond = input(x, y) > 10;
                uint8_t correct = cond ? 255 : 0;
                if (correct != output(x, y)) {
                    printf("output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                    return -1;
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
            in_wide(x, y) = cast(wide, y + x*3);
            in_wide.compute_root();

            Func in_narrow;
            in_narrow(x, y) = cast(narrow, x*y + x - 17);
            in_narrow.compute_root();

            Func f;
            f(x, y) = select(in_narrow(x, y) > 10, in_wide(x, y*2), in_wide(x, y*2+1));

            Func cpu;
            cpu(x, y) = f(x, y);

            Func gpu;
            gpu(x, y) = f(x, y);

            Func out;
            out(x, y) = {cast<uint32_t>(cpu(x, y)), cast<uint32_t>(gpu(x, y))};

            cpu.compute_root();
            gpu.compute_root();

            Target target = get_jit_target_from_environment();
            if (target.has_gpu_feature()) {
                gpu.gpu_tile(x, y, 16, 16).vectorize(Var::gpu_threads(), 4);
            } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
                gpu.hexagon().vectorize(x, 128);
            } else {
                // Just test vectorization
                gpu.vectorize(x, 8);
            }

            Realization r = out.realize(input.width(), input.height(), target);
            Image<uint32_t> cpu_output = r[0];
            Image<uint32_t> gpu_output = r[1];

            for (int y = 0; y < input.height(); y++) {
                for (int x = 0; x < input.width(); x++) {
                    if (cpu_output(x, y) != gpu_output(x, y)) {
                        printf("gpu_output(%d, %d) = %d instead of %d\n",
                               x, y, gpu_output(x, y), cpu_output(x, y));
                        return -1;
                    }
                }
            }
        }
    }


    printf("Success!\n");
    return 0;

}
