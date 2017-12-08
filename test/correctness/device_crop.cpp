#include "Halide.h"
#include "HalideBuffer.h"

using namespace Halide;

Halide::Runtime::Buffer<int32_t> make_gpu_buffer() {
  Var x, y;
  Func f;
  f(x, y) = x + y * 256;

  Var xi, yi;
  f.gpu_tile(x, y, xi, yi, 8, 8);

  Buffer<int32_t>  result = f.realize(128, 128);
  return *result.get();
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    if (!target.has_gpu_feature()) {
        printf("This is a gpu-specific test. Skipping it\n");
        return 0;
    }

    // Test in-place cropping.
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer();
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        gpu_buf.crop({ {32, 64} , {32, 64} });
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        gpu_buf.copy_to_host();
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 64; j++) {
              assert(gpu_buf(32 + i, 32 + j) == (i + 32) + 256 * (j + 32));
            }
        }
    }

    // Test nondestructive cropping.
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer();
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> cropped = gpu_buf.cropped({ {32, 64} , {32, 64} });
        assert(cropped.raw_buffer()->device_interface != nullptr);

        cropped.copy_to_host();
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 64; j++) {
                assert(cropped(32 + i, 32 + j) == (i + 32) + 256 * (j + 32));
            }
        }
    }

    // Test crop of a crop
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer();
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> cropped = gpu_buf.cropped({ {32, 64} , {32, 64} });
        assert(cropped.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> cropped2 = cropped.cropped({ {40, 16} , {40, 16} });
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

    // Test parent going out of scope before crop.
    {
        Halide::Runtime::Buffer<int32_t> cropped;

        {
            Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer();
            assert(gpu_buf.raw_buffer()->device_interface != nullptr);

            cropped = gpu_buf.cropped({ {32, 64} , {32, 64} });
            assert(cropped.raw_buffer()->device_interface != nullptr);
        }

        cropped.copy_to_host();
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 64; j++) {
                assert(cropped(32 + i, 32 + j) == (i + 32) + 256 * (j + 32));
            }
        }
    }

    return 0;
}
