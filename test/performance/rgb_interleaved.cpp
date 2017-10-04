#include "Halide.h"
#include <cstdio>
#include <memory>
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

void test_deinterleave() {
    ImageParam src(UInt(8), 3);
    Func dst;
    Var x, y, c;

    dst(x, y, c) = src(x, y, c);

    src.dim(0).set_stride(3)
        .dim(2).set_stride(1).set_bounds(0, 3);

    // This is the default format for Halide, but made explicit for illustration.
    dst.output_buffer()
        .dim(0).set_stride(1)
        .dim(2).set_extent(3);

    dst.reorder(c, x, y).unroll(c);
    dst.vectorize(x, 16);


    // Allocate two 16 megapixel, 3 channel, 8-bit images -- input and output

    // Setup src to be RGB interleaved, with no extra padding between channels or rows.
    Buffer<uint8_t> src_image = Buffer<uint8_t>::make_interleaved(1 << 12, 1 << 12, 3);

    // Setup dst to be planar, with no extra padding between channels or rows.
    Buffer<uint8_t> dst_image(1 << 12, 1 << 12, 3);

    src_image.for_each_element([&](int x, int y) {
            src_image(x, y, 0) = 0;
            src_image(x, y, 1) = 128;
            src_image(x, y, 2) = 255;
        });
    dst_image.fill(0);

    src.set(src_image);

    dst.compile_jit();

    // Warm up caches, etc.
    dst.realize(dst_image);

    double t1 = benchmark([&]() {
        dst.realize(dst_image);
    });

    printf("Interleaved to planar bandwidth %.3e byte/s.\n",
           dst_image.number_of_elements() / t1);

    dst_image.for_each_element([&](int x, int y) {
            assert(dst_image(x, y, 0) == 0);
            assert(dst_image(x, y, 1) == 128);
            assert(dst_image(x, y, 2) == 255);
        });

    // Setup a semi-planar output case.
    dst_image = Buffer<uint8_t>(1 << 12, 3, 1 << 12);
    dst_image.transpose(1, 2);
    dst_image.fill(0);

    double t2 = benchmark([&]() {
        dst.realize(dst_image);
    });

    dst_image.for_each_element([&](int x, int y) {
            assert(dst_image(x, y, 0) == 0);
            assert(dst_image(x, y, 1) == 128);
            assert(dst_image(x, y, 2) == 255);
        });

    printf("Interleaved to semi-planar bandwidth %.3e byte/s.\n",
           dst_image.number_of_elements() / t2);
}

void test_interleave(bool fast) {
    ImageParam src(UInt(8), 3);
    Func dst;
    Var x, y, c;

    dst(x, y, c) = src(x, y, c);

    // This is the default format for Halide, but made explicit for illustration.
    src.dim(0).set_stride(1).dim(2).set_extent(3);

    dst.output_buffer()
        .dim(0).set_stride(3)
        .dim(2).set_stride(1).set_bounds(0, 3);

    if( fast ) {
        dst.reorder(c, x, y).bound(c, 0, 3).unroll(c);
        dst.vectorize(x, 16);
    } else {
        dst.reorder(c, x, y).vectorize(x, 16);
    }

    // Allocate two 16 megapixel, 3 channel, 8-bit images -- input and output

    // Setup src to be planar
    Buffer<uint8_t> src_image(1 << 12, 1 << 12, 3);

    // Setup dst to be interleaved
    Buffer<uint8_t> dst_image = Buffer<uint8_t>::make_interleaved(1 << 12, 1 << 12, 3);

    src_image.for_each_element([&](int x, int y) {
            src_image(x, y, 0) = 0;
            src_image(x, y, 1) = 128;
            src_image(x, y, 2) = 255;
        });
    dst_image.fill(0);

    src.set(src_image);

    if (fast) {
        dst.compile_to_lowered_stmt("rgb_interleave_fast.stmt", dst.infer_arguments());
    } else {
        dst.compile_to_lowered_stmt("rgb_interleave_slow.stmt", dst.infer_arguments());
    }

    // Warm up caches, etc.
    dst.realize(dst_image);

    double t = benchmark([&]() {
        dst.realize(dst_image);
    });

    printf("Planar to interleaved bandwidth %.3e byte/s.\n",
           dst_image.number_of_elements() / t);

    dst_image.for_each_element([&](int x, int y) {
            assert(dst_image(x, y, 0) == 0);
            assert(dst_image(x, y, 1) == 128);
            assert(dst_image(x, y, 2) == 255);
        });
}

int main(int argc, char **argv) {
    test_deinterleave();
    test_interleave(false);
    test_interleave(true);
    printf("Success!\n");
    return 0;
}
