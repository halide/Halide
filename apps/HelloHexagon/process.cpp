#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include <malloc.h>

#include "pipeline.h"

#include "HalideRuntime.h"

// Helper function to access element (x, y, z, w) of a buffer_t.
template <typename T>
T& BufferAt(buffer_t* buf,
            int x,
            int y = 0,
            int z = 0,
            int w = 0) {
  return *reinterpret_cast<T*>(
      buf->host + buf->elem_size * ((x - buf->min[0]) * buf->stride[0] +
                                    (y - buf->min[1]) * buf->stride[1] +
                                    (z - buf->min[2]) * buf->stride[2] +
                                    (w - buf->min[3]) * buf->stride[3]));
}

template <typename T>
T clamp(T x, T min, T max) {
    if (x < min) x = min;
    if (x > max) x = max;
    return x;
}

int main(int argc, char **argv) {
    const int W = 1024;
    const int H = 1024;

    // Allocate buffers for the input and output image.
    uint8_t* in_host = (uint8_t*)memalign(4096, W*H*3);
    uint8_t* out_host = (uint8_t*)memalign(4096, W*H*3);

    // Set up the buffer_t describing the input buffer.
    buffer_t in = { 0 };
    in.host = in_host;
    in.elem_size = 1;
    in.extent[0] = W;
    in.extent[1] = H;
    in.extent[2] = 3;
    in.stride[0] = 1;
    in.stride[1] = W;
    in.stride[2] = W * H;

    // The output buffer has the same description, but a different
    // host pointer.
    buffer_t out = in;
    out.host = out_host;

    // Fill the input buffer with random data.
    for (int i = 0; i < W * H * 3; i++) {
        in_host[i] = static_cast<uint8_t>(rand());
    }

    // Indicate that we've replaced the data in the input buffer.
    in.host_dirty = true;

    printf("Running pipeline... ");
    int result = pipeline(&in, &out);
    printf("done: %d\n", result);
    if (result != 0) {
        free(in_host);
        free(out_host);
        return result;
    }

    printf("halide_copy_to_host... ");
    result = halide_copy_to_host(NULL, &out);
    printf("done: %d\n", result);
    if (result != 0) {
        free(in_host);
        free(out_host);
        return result;
    }

    // Validate that the algorithm did what we expect.
    const uint16_t gaussian5[] = { 1, 4, 6, 4, 1 };
    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                uint16_t blur = 0;
                for (int rx = -2; rx <= 2; rx++) {
                    uint16_t blur_y = 0;
                    for (int ry = -2; ry <= 2; ry++) {
                        uint16_t in_rxy =
                            BufferAt<uint8_t>(&in, clamp(x + rx, 0, W - 1), clamp(y + ry, 0, H - 1), c);
                        blur_y += in_rxy * gaussian5[ry + 2];
                    }
                    blur_y += 8;
                    blur_y /= 16;

                    blur += blur_y * gaussian5[rx + 2];
                }
                blur += 8;
                blur /= 16;

                uint8_t out_xy = BufferAt<uint8_t>(&out, x, y, c);
                if (blur != out_xy) {
                    printf("Mismatch at %d %d %d: %d != %d\n", x, y, c, out_xy, blur);
                    return -1;
                }
            }
        }
    }

    free(in_host);
    free(out_host);
    return result;
}
