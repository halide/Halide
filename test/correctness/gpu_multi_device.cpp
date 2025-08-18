#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {

struct MultiDevicePipeline {
    Var x, y, c, xi, yi;
    Func stage[5];
    size_t current_stage;

    MultiDevicePipeline(const Func &input, const Target &target) {
        current_stage = 0;

        stage[current_stage](x, y, c) = input(x, y, c);
        current_stage++;

        if (target.has_feature(Target::OpenCL)) {
            stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
            stage[current_stage]
                .compute_root()
                .reorder(c, x, y)
                .gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::Auto, DeviceAPI::OpenCL);
            current_stage++;
        }
        if (target.has_feature(Target::CUDA)) {
            stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
            stage[current_stage]
                .compute_root()
                .reorder(c, x, y)
                .gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::Auto, DeviceAPI::CUDA);
            current_stage++;
        }
        if (target.has_feature(Target::Metal)) {
            stage[current_stage](x, y, c) = stage[current_stage - 1](x, y, c) + 69;
            stage[current_stage]
                .compute_root()
                .reorder(c, x, y)
                .gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::Auto, DeviceAPI::Metal);
            current_stage++;
        }
    }

    void run(Buffer<float> &result) {
        stage[current_stage - 1].realize(result);
        if (result.copy_to_host() != halide_error_code_success) {
            fprintf(stderr, "copy_to_host failed\n");
            exit(1);
        }
        if (result.device_free() != halide_error_code_success) {
            fprintf(stderr, "device_free failed\n");
            exit(1);
        }
        result.set_host_dirty();
    }
};

class GPUMultiDeviceTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
    Var x{"x"}, y{"y"}, c{"c"};
    Func const_input = [&] {
        Func f{"const_input"};
        f(x, y, c) = 42.0f;
        return f;
    }();
    MultiDevicePipeline pipe{const_input, target};

    void SetUp() override {
        if (pipe.current_stage < 3) {
            GTEST_SKIP() << "Need two or more GPU targets enabled.";
        }
    }

    static void Check(const Buffer<float> &result, size_t stages) {
        for (int i = 0; i < 100; i++) {
            for (int j = 0; j < 100; j++) {
                for (int k = 0; k < 3; k++) {
                    float correct = 42.0f + stages * 69;
                    ASSERT_EQ(result(i, j, k), correct);
                }
            }
        }
    }
};

}  // namespace

TEST_F(GPUMultiDeviceTest, ConstInput) {
    Buffer<float> output(100, 100, 3);
    pipe.run(output);
    Check(output, pipe.current_stage - 1);
}

TEST_F(GPUMultiDeviceTest, ChainedBuffers) {
    // Same as above
    Buffer<float> intm(100, 100, 3);
    pipe.run(intm);

    // --------------------------------------------

    ImageParam gpu_buffer(Float(32), 3);
    gpu_buffer.dim(2).set_bounds(0, 3);
    gpu_buffer.set(intm);

    MultiDevicePipeline pipe2(gpu_buffer, target);

    Buffer<float> output(100, 100, 3);
    pipe2.run(output);

    Check(output, pipe.current_stage + pipe2.current_stage - 2);
}
