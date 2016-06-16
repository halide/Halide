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

    Image<uint8_t> input(64, 64);

    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            input(x, y) = y*64 + x;
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
            f.hexagon().vectorize(x, 64);
        } else {
            f.vectorize(x, 8);
        }

        Image<uint8_t> output = f.realize(64, 64, target);

        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
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
            f.hexagon().vectorize(x, 64);
        } else {
            f.vectorize(x, 8);
        }

        Image<uint8_t> output = f.realize(64, 64, target);

        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
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
            f.hexagon().vectorize(x, 64);
        } else {
            f.vectorize(x, 8);
        }

        Image<uint8_t> output = f.realize(64, 64, target);

        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
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
    {
        Func f;
        f(x, y) = select(input(x, y) > 10, u16(255), u16(0));

        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, 16, 16).vectorize(Var::gpu_threads(), 4);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            f.hexagon().vectorize(x, 64);
        } else {
            f.vectorize(x, 8);
        }

        Image<uint16_t> output = f.realize(64, 64, target);

        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                bool cond = input(x, y) > 10;
                uint16_t correct = cond ? 255 : 0;
                if (correct != output(x, y)) {
                    printf("output(%d, %d) = %d instead of %d\n", x, y, output(x, y), correct);
                    return -1;
                }
            }
        }
    }


    printf("Success!\n");
    return 0;

}
