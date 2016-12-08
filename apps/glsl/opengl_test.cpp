#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HalideRuntime.h"
#include "HalideRuntimeOpenGL.h"

#include "halide_blur_glsl.h"
#include "halide_ycc_glsl.h"


class Image {
public:
    enum Layout {
        Interleaved, Planar
    };

    halide_buffer_t buf;
    halide_dimension_t shape[3];

    Image(int w, int h, int c, Layout layout = Interleaved) {
        memset(&buf, 0, sizeof(buf));
        memset(&shape, 0, sizeof(shape));
        buf.dim = shape;
        shape[0].extent = w;
        shape[1].extent = h;
        shape[2].extent = c;
        buf.type = halide_type_of<uint8_t>();
        buf.dimensions = 3;

        if (layout == Interleaved) {
            shape[0].stride = shape[2].extent;
            shape[1].stride = shape[0].extent * shape[0].stride;
            shape[2].stride = 1;
        } else {
            shape[0].stride = 1;
            shape[1].stride = shape[0].extent * shape[0].stride;
            shape[2].stride = shape[1].extent * shape[1].stride;
        }
        size_t size = w * h * c;
        buf.host = (uint8_t*)malloc(size);
        memset(buf.host, 0, size);
        buf.set_host_dirty(true);
    }
    ~Image() {
        free(buf.host);
    }
};

void test_blur() {
    const int W = 12, H = 32, C = 3;
    Image input(W, H, C, Image::Planar);
    Image output(W, H, C, Image::Planar);

    fprintf(stderr, "test_blur\n");
    halide_blur_glsl(&input.buf, &output.buf);
    fprintf(stderr, "test_blur complete\n");
}

void test_ycc() {
    const int W = 12, H = 32, C = 3;
    Image input(W, H, C, Image::Planar);
    Image output(W, H, C, Image::Planar);

    fprintf(stderr, "test_ycc\n");
    halide_ycc_glsl(&input.buf, &output.buf);
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
