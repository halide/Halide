#include <Halide.h>
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
            stage[current_stage].compute_root().reorder(c, x, y).gpu_tile(x, y, 32, 32, DeviceAPI::OpenCL);
            current_stage++;
        }
        if (jit_target.has_feature(Target::CUDA)) {
            stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
            stage[current_stage].compute_root().reorder(c, x, y).gpu_tile(x, y, 32, 32, DeviceAPI::CUDA);
            current_stage++;
        }
        if (jit_target.has_feature(Target::OpenGL)) {
            stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
            stage[current_stage].compute_root().bound(c, 0, 3).reorder(c, x, y).glsl(x, y, c).vectorize(c);
            current_stage++;
        }
    }

    void run(Image<uint8_t> &result) {
        stage[current_stage - 1].realize(result);
    }

    bool verify(const Image<uint8_t> &result, size_t stages, const char * test_case) {
        for (int i = 0; i < 100; i++) {
            for (int j = 0; j < 100; j++) {
                for (int k = 0; k < 3; k++) {
                    int correct = 42 + stages * 69;
                    if (result(i, j, k) != correct) {
                        printf("result(%d, %d, %d) = %d instead of %d. (%s).\n", i, j, k, result(i, j, k), correct, test_case);
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
    const_input(x, y, c) = cast<uint8_t>(42);

    {
        MultiDevicePipeline pipe1(const_input);
        if (pipe1.current_stage < 3) {
            printf("One or fewer gpu targets enabled. Skipping test.\n");
            return 0;
        }

        Image<uint8_t> output1(100, 100, 3);
        pipe1.run(output1);

        if (!pipe1.verify(output1, pipe1.current_stage - 1, "const input")) {
            return -1;
        }
    }

    {
        MultiDevicePipeline pipe2(const_input);

        ImageParam gpu_buffer(UInt(8), 3);
        Func buf_input;
        buf_input(x, y, c) = gpu_buffer(x, y, c);
        MultiDevicePipeline pipe3(buf_input);

        Image<uint8_t> output2(100, 100, 3);
        pipe2.run(output2);

        Image<uint8_t> output3(100, 100, 3);
        gpu_buffer.set(output2);
        pipe3.run(output3);

        pipe3.verify(output3, pipe2.current_stage + pipe3.current_stage - 2, "chained buffers");
    }

    printf("Success!\n");
    return 0;
}
