#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>

#include "scale.h"

#include "HalideRuntime.h"

void *_mm_malloc(size_t size, size_t alignment) {
    char *buf = (char *)malloc(size + alignment);
    if (!buf) {
        printf("malloc failed\n");
        fflush(stdout);
        return 0;
    }
    char *aligned = (char *)((((uintptr_t)buf) + alignment) & ~(alignment - 1));
    ((char **)aligned)[-1] = buf;
    return aligned;
}

void _mm_free(void *x) {
    free(((char **)x)[-1]);
}

template <typename T>
T& BufferAt(buffer_t* buf,
            int i0,
            int i1 = 0,
            int i2 = 0,
            int i3 = 0) {
  return *reinterpret_cast<T*>(
      buf->host + buf->elem_size * ((i0 - buf->min[0]) * buf->stride[0] +
                                    (i1 - buf->min[1]) * buf->stride[1] +
                                    (i2 - buf->min[2]) * buf->stride[2] +
                                    (i3 - buf->min[3]) * buf->stride[3]));
}

template <typename T>
T clamp(T x, T min, T max) {
    if (x < min) x = min;
    if (x > max) x = max;
    return x;
}

int main(int argc, char **argv) {
    const int W = 128*16;
    const int H = 128*16;

    printf("Hello\n");

    uint8_t* in_host = (uint8_t*)_mm_malloc(W*H*3, 4096);
    uint8_t* out_host = (uint8_t*)_mm_malloc(W*H*3, 4096);
    for (int i = 0; i < W * H * 3; i++) {
        in_host[i] = static_cast<uint8_t>(rand());
    }

    printf("Allocated buffers\n");

    buffer_t in = { 0 };
    in.host = in_host;
    in.elem_size = 1;
    in.extent[0] = W;
    in.extent[1] = H;
    in.extent[2] = 3;
    in.stride[0] = 1;
    in.stride[1] = W;
    in.stride[2] = W * H;

    int radius = 3;

    buffer_t out = in;
    out.host = out_host;

    in.host_dirty = true;

    printf("Running pipeline...");
    int result = scale(&in, &out);
    printf("done: %d\n", result);
    if (result != 0) {
        _mm_free(in_host);
        _mm_free(out_host);
        return result;
    }
    printf("dev_dirty: %d\n", (int)out.dev_dirty);
    printf("halide_copy_to_host...");
    result = halide_copy_to_host(NULL, &out);
    printf("done: %d\n", result);
    if (result != 0) {
        _mm_free(in_host);
        _mm_free(out_host);
        return result;
    }


    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                uint16_t blur = 0;
                for (int ry = -radius; ry <= radius; ry++) {
                    blur += BufferAt<uint8_t>(&in, x, clamp(y + ry, 0, H - 1), c);
                }
                uint8_t a = static_cast<uint8_t>(blur/(2*radius + 1));
                uint8_t b = BufferAt<uint8_t>(&out, x, y, c);

                if (a != b) {
                    printf("Mismatch at %d %d %d: %d != %d\n", x, y, c, a, b);
                    return -1;
                }
            }
        }
    }

    _mm_free(in_host);
    _mm_free(out_host);
    return result;
}
