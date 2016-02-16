#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HalideRuntime.h"


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

#include "blur.h"
#include "ycc.h"

void test_blur() {
    const int W = 12, H = 32, C = 3;
    Image input(W, H, C, Image::Planar);
    Image output(W, H, C, Image::Planar);

    fprintf(stderr, "test_blur\n");
    blur(&input.buf, &output.buf);
    fprintf(stderr, "test_blur complete\n");
}

void test_ycc() {
    const int W = 12, H = 32, C = 3;
    Image input(W, H, C, Image::Planar);
    Image output(W, H, C, Image::Planar);

    fprintf(stderr, "test_ycc\n");
    ycc(&input.buf, &output.buf);
    fprintf(stderr, "Ycc complete\n");
}

int main(int argc, char* argv[]) {
    test_blur();
    test_ycc();
}
