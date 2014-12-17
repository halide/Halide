#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, c;
    Func stage[4];
    size_t current_stage = 0;

    stage[current_stage](x, y, c) = 42;
    current_stage++;

    Target jit_target(get_jit_target_from_environment());
    if (jit_target.has_feature(Target::OpenCL)) {
        stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
        stage[current_stage].compute_root().gpu_tile(x, y, 32, 32, Device_CUDA);
        current_stage++;
    }
    if (jit_target.has_feature(Target::CUDA)) {
        stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
        stage[current_stage].compute_root().gpu_tile(x, y, 32, 32, Device_OpenCL);
        current_stage++;
    }
    if (jit_target.has_feature(Target::OpenGL)) {
        stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
        stage[current_stage].compute_root().bound(c, 0, 3).glsl(x, y, c).vectorize(c);
        current_stage++;
    }

    if (current_stage < 3) {
        printf("One or fewer gpu target enabled. Skipping test.\n");
        return 0;
    }

    Image<int> output(100, 100, 3);
    stage[current_stage - 1].realize(output);

    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            int correct = 42 + (current_stage - 1) * 69;
            if (output(i, j) != correct) {
                printf("output(%d, %d) = %d instead of %d\n", i, j, output(i, j), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
