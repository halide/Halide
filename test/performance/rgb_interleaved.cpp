#include "Halide.h"
#include <cstdio>
#include <memory>
#include "benchmark.h"

using namespace Halide;

void set_values(Image<uint8_t> im, uint8_t r, uint8_t g, uint8_t b) {
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            im(x, y, 0) = r;
            im(x, y, 1) = g;
            im(x, y, 2) = b;
        }
    }
}

void check_values(Image<uint8_t> im, uint8_t r, uint8_t g, uint8_t b) {
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            if (!(im(x, y, 0) == r &&
                  im(x, y, 1) == g &&
                  im(x, y, 2) == b)) {
                printf("im(%d, %d) = {%d, %d, %d} instead of {%d, %d, %d}\n",
                       x, y, im(x, y, 0), im(x, y, 1), im(x, y, 2), r, g, b);
                exit(-1);
            }
        }
    }
}

Image<uint8_t> make_planar(int size, uint8_t r, uint8_t g, uint8_t b) {
    Image<uint8_t> im(size, size, 3);
    set_values(im, r, g, b);
    return im;
}

Image<uint8_t> make_interleaved(int size, uint8_t r, uint8_t g, uint8_t b) {
    // The image type is naturally planar, but we can sneakily make an interleaved one like so...
    Image<uint8_t> im(3, size, size);

    // Swap the dimension descriptors in the raw buffer...
    std::swap(im.raw_buffer()->dim[0], im.raw_buffer()->dim[1]);
    std::swap(im.raw_buffer()->dim[1], im.raw_buffer()->dim[2]);

    // But the image type doesn't except those strides to change, and
    // actually caches them for fast access. We'll need to turn it
    // to a buffer and back to an image to get it to grab them again.
    im = Image<uint8_t>(Buffer(im));

    set_values(im, r, g, b);
    return im;
}

Image<uint8_t> make_semi_planar(int size, uint8_t r, uint8_t g, uint8_t b) {
    Image<uint8_t> im(size, 3, size);

    std::swap(im.raw_buffer()->dim[1], im.raw_buffer()->dim[2]);

    im = Image<uint8_t>(Buffer(im));

    set_values(im, r, g, b);
    return im;
}

void test_deinterleave() {
    ImageParam src(UInt(8), 3);
    Func dst;
    Var x, y, c;

    dst(x, y, c) = src(x, y, c);

    // src is interleaved
    src
        .dim(0).set_stride(3)
        .dim(2).set_stride(1).set_extent(3);

    // dst is planar
    dst.output_buffer()
        .dim(0).set_stride(1)
        .dim(2).set_extent(3);

    dst.reorder(c, x, y).unroll(c);
    dst.vectorize(x, 16);

    // Allocate two 16 megapixel, 3 channel, 8-bit images -- input and output
    const int32_t size = (1 << 12);
    const int32_t buffer_size = size*size*3;

    // Setup src to be RGB interleaved, with no extra padding between
    // channels or rows.
    Image<uint8_t> src_image = make_interleaved(size, 0, 128, 255);

    // Setup dst to be planar, with no extra padding between channels or rows.
    Image<uint8_t> dst_image = make_planar(size, 0, 0, 0);

    src.set(src_image);

    double t1 = benchmark(1, 20, [&]() {
        dst.realize(dst_image);
    });

    printf("Interleaved to planar bandwidth %.3e byte/s.\n", buffer_size / t1);
    check_values(dst_image, 0, 128, 255);

    // Setup a semi-planar output case.
    dst_image = make_semi_planar(size, 0, 0, 0);

    double t2 = benchmark(3, 3, [&]() {
        dst.realize(dst_image);
    });
    check_values(dst_image, 0, 128, 255);

    printf("Interleaved to semi-planar bandwidth %.3e byte/s.\n", buffer_size / t2);
}

void test_interleave(bool fast) {
    ImageParam src(UInt(8), 3);
    Func dst;
    Var x, y, c;

    dst(x, y, c) = src(x, y, c);

    // src is planar
    src
        .dim(0).set_stride(1)
        .dim(2).set_extent(3);

    // dst is interleaved
    dst.output_buffer()
        .dim(0).set_stride(3)
        .dim(2).set_stride(1).set_bounds(0, 3);

    if (fast) {
        dst.reorder(c, x, y).bound(c, 0, 3).unroll(c);
        dst.vectorize(x, 16);
    } else {
        dst.reorder(c, x, y).vectorize(x, 16);
    }

    // Allocate two 16 megapixel, 3 channel, 8-bit images -- input and output
    const int32_t size = (1 << 12);
    const int32_t buffer_size = size*size*3;

    // Setup src to be planar, with no extra padding between channels or rows.
    Image<uint8_t> src_image = make_planar(size, 0, 128, 255);

    // Setup dst to be RGB interleaved, with no extra padding between
    // channels or rows. Do it by making a 3 x size x size planar
    // buffer and swapping two of the dimension descriptors.
    Image<uint8_t> dst_image = make_interleaved(size, 0, 0, 0);

    src.set(src_image);

    double t = benchmark(3, 3, [&]() {
        dst.realize(dst_image);
    });

    printf("Planar to interleaved bandwidth %.3e byte/s.\n", buffer_size / t);

    check_values(dst_image, 0, 128, 255);
}

int main(int argc, char **argv) {
    test_deinterleave();
    test_interleave(false);
    test_interleave(true);
    printf("Success!\n");
    return 0;
}
