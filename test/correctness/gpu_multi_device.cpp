#include "Halide.h"
#include <stdio.h>

using namespace Halide;

struct MultiDevicePipeline {
    Var x, y, c;
    Func stage[4];
    size_t current_stage;

    MultiDevicePipeline(Func input) {
        current_stage = 0;

        stage[current_stage](x, y, c) = input(x, y, c);
        current_stage++;

        Target jit_target(get_jit_target_from_environment());
        if (jit_target.has_feature(Target::OpenCL)) {
            stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
            stage[current_stage].compute_root().reorder(c, x, y).gpu_tile(x, y, 32, 32, TailStrategy::Auto, DeviceAPI::OpenCL);
            current_stage++;
        }
        if (jit_target.has_feature(Target::CUDA)) {
            stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
            stage[current_stage].compute_root().reorder(c, x, y).gpu_tile(x, y, 32, 32, TailStrategy::Auto, DeviceAPI::CUDA);
            current_stage++;
        }
        if (jit_target.has_feature(Target::OpenGL)) {
            stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
            stage[current_stage].compute_root().bound(c, 0, 3).reorder(c, x, y).glsl(x, y, c).vectorize(c);
            current_stage++;
        }
        if (jit_target.has_feature(Target::OpenGLCompute)) {
            stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
            stage[current_stage].compute_root().reorder(c, x, y).gpu_tile(x, y, 32, 32, TailStrategy::Auto, DeviceAPI::OpenGLCompute);
            current_stage++;
        }
    }

    void run(Buffer<float> &result) {
        stage[current_stage - 1].realize(result);
    }

    bool verify(const Buffer<float> &result, size_t stages, const char * test_case) {
        for (int i = 0; i < 100; i++) {
            for (int j = 0; j < 100; j++) {
                for (int k = 0; k < 3; k++) {
                    float correct = 42.0f + stages * 69;
                    if (result(i, j, k) != correct) {
                        printf("result(%d, %d, %d) = %f instead of %f. (%s).\n", i, j, k, result(i, j, k), correct, test_case);
                        return false;
                    }
                }
            }
        }
        return true;
    }
};

int main(int argc, char **argv) {
    Var x, y, c;
    Func const_input;
    const_input(x, y, c) = 42.0f;

    {
        MultiDevicePipeline pipe1(const_input);
        if (pipe1.current_stage < 3) {
            printf("One or fewer gpu targets enabled. Skipping test.\n");
            return 0;
        }

        Buffer<float> output1(100, 100, 3);
        pipe1.run(output1);

        if (!pipe1.verify(output1, pipe1.current_stage - 1, "const input")) {
            return -1;
        }
    }

    {
        MultiDevicePipeline pipe2(const_input);

        ImageParam gpu_buffer(Float(32), 3);
        gpu_buffer.dim(2).set_bounds(0, 3);
        Func buf_input;
        buf_input(x, y, c) = gpu_buffer(x, y, c);
        MultiDevicePipeline pipe3(buf_input);

        Buffer<float> output2(100, 100, 3);
        pipe2.run(output2);

        Buffer<float> output3(100, 100, 3);
        gpu_buffer.set(output2);
        pipe3.run(output3);

        pipe3.verify(output3, pipe2.current_stage + pipe3.current_stage - 2, "chained buffers");
    }

    printf("Success!\n");
    return 0;
}
