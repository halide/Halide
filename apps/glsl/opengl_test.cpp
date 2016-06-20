#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HalideRuntime.h"
#include "HalideRuntimeOpenGL.h"


class Image {
public:
    enum Layout {
        Interleaved, Planar
    };

    buffer_t buf;

    Image(int w, int h, int c, int elem_size, Layout layout = Interleaved) {
        memset(&buf, 0, sizeof(buffer_t));
        buf.extent[0] = w;
        buf.extent[1] = h;
        buf.extent[2] = c;
        buf.elem_size = elem_size;

        if (layout == Interleaved) {
            buf.stride[0] = buf.extent[2];
            buf.stride[1] = buf.extent[0] * buf.stride[0];
            buf.stride[2] = 1;
        } else {
            buf.stride[0] = 1;
            buf.stride[1] = buf.extent[0] * buf.stride[0];
            buf.stride[2] = buf.extent[1] * buf.stride[1];
        }
        size_t size = w * h * c * elem_size;
        buf.host = (uint8_t*)malloc(size);
        memset(buf.host, 0, size);
        buf.host_dirty = true;
    }
    ~Image() {
        free(buf.host);
    }
};

#include "blur.h"
#include "ycc.h"

void test_blur() {
    const int W = 12, H = 32, C = 3;
    Image input(W, H, C, sizeof(uint8_t), Image::Planar);
    Image output(W, H, C, sizeof(uint8_t), Image::Planar);

    fprintf(stderr, "test_blur\n");
    blur(&input.buf, &output.buf);
    fprintf(stderr, "test_blur complete\n");
}

void test_ycc() {
    const int W = 12, H = 32, C = 3;
    Image input(W, H, C, sizeof(uint8_t), Image::Planar);
    Image output(W, H, C, sizeof(uint8_t), Image::Planar);

    fprintf(stderr, "test_ycc\n");
    ycc(&input.buf, &output.buf);
    fprintf(stderr, "Ycc complete\n");
}

void test_device_sync() {
    const int W = 12, H = 32, C = 3;
    Image temp(W, H, C, sizeof(uint8_t), Image::Planar);

    int result = halide_device_malloc(nullptr, &temp.buf, halide_opengl_device_interface());
    if (result != 0) {
        fprintf(stderr, "halide_device_malloc failed with return %d.\n", result);
    } else {
        result = halide_device_sync(nullptr, &temp.buf);
        if (result != 0) {
            fprintf(stderr, "halide_device_sync failed with return %d.\n", result);
        } else {
            fprintf(stderr, "Test device sync complete.\n");
        }
    }
}

int main(int argc, char* argv[]) {
    test_blur();
    test_ycc();
    test_device_sync();
}
