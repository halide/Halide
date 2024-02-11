#include "Halide.h"
#include "HalideBuffer.h"

using namespace Halide;

Halide::Runtime::Buffer<int32_t> make_gpu_buffer(bool hexagon_rpc) {
    Var x, y;
    Func f;
    f(x, y) = x + y * 256;

    if (hexagon_rpc) {
        f.hexagon();
    } else {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 8, 8);
    }

    Buffer<int32_t> result = f.realize({128, 128});
    return *result.get();
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    bool hexagon_rpc = (target.arch != Target::Hexagon) &&
                       target.has_feature(Target::HVX);

    if (!hexagon_rpc && !target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    printf("Test in-place cropping.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        gpu_buf.crop({{32, 64}, {32, 64}});
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        gpu_buf.copy_to_host();
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 64; j++) {
                assert(gpu_buf(32 + i, 32 + j) == (i + 32) + 256 * (j + 32));
            }
        }
    }

    printf("Test nondestructive cropping.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> cropped = gpu_buf.cropped({{32, 64}, {32, 64}});
        assert(cropped.raw_buffer()->device_interface != nullptr);

        cropped.copy_to_host();
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 64; j++) {
                assert(cropped(32 + i, 32 + j) == (i + 32) + 256 * (j + 32));
            }
        }
    }

    printf("Test crop of a crop\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> cropped = gpu_buf.cropped({{32, 64}, {32, 64}});
        assert(cropped.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> cropped2 = cropped.cropped({{40, 16}, {40, 16}});
        assert(cropped2.raw_buffer()->device_interface != nullptr);

        cropped.copy_to_host();
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 64; j++) {
                assert(cropped(32 + i, 32 + j) == (i + 32) + 256 * (j + 32));
            }
        }

        cropped2.copy_to_host();
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                assert(cropped2(40 + i, 40 + j) == (i + 40) + 256 * (j + 40));
            }
        }
    }

    printf("Test parent going out of scope before crop.\n");
    {
        Halide::Runtime::Buffer<int32_t> cropped;

        {
            Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
            assert(gpu_buf.raw_buffer()->device_interface != nullptr);

            cropped = gpu_buf.cropped({{32, 64}, {32, 64}});
            assert(cropped.raw_buffer()->device_interface != nullptr);
        }

        cropped.copy_to_host();
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 64; j++) {
                assert(cropped(32 + i, 32 + j) == (i + 32) + 256 * (j + 32));
            }
        }
    }

    printf("Test realizing to/from crop.\n");
    {
        ImageParam in(Int(32), 2);
        Var x, y;
        Func f;
        f(x, y) = in(x, y) + 42;

        Var xi, yi;
        if (hexagon_rpc) {
            f.hexagon();
        } else {
            f.gpu_tile(x, y, xi, yi, 8, 8);
        }

        Halide::Buffer<int32_t> gpu_input = make_gpu_buffer(hexagon_rpc);
        Halide::Buffer<int32_t> gpu_output = make_gpu_buffer(hexagon_rpc);

        gpu_input.crop({{64, 64}, {64, 64}});
        gpu_output.crop({{64, 64}, {64, 64}});

        in.set(gpu_input);

        f.realize(gpu_output, target);

        gpu_output.copy_to_host();
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 64; j++) {
                assert(gpu_output(64 + i, 64 + j) == (i + 64) + 256 * (j + 64) + 42);
            }
        }
    }

    printf("Success!\n");

    return 0;
}
