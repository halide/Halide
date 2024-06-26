#include "Halide.h"
#include "halide_benchmark.h"
#include <algorithm>
#include <cstdio>
#include <memory>

using namespace Halide;
using namespace Halide::Tools;

double test_copy(Buffer<uint8_t> src, Buffer<uint8_t> dst) {
    Var x, y, c;
    Func f;
    f(x, y, c) = src(x, y, c);

    for (int i = 0; i < 3; i++) {
        f.output_buffer()
            .dim(i)
            .set_stride(dst.dim(i).stride())
            .set_extent(dst.dim(i).extent())
            .set_min(dst.dim(i).min());
    }

    if (dst.stride(0) == 1 && src.stride(0) == 1) {
        // packed -> packed
        f.vectorize(x, 16);
    } else if (dst.stride(0) == 3 && src.stride(0) == 3) {
        // packed -> packed
        Var fused("fused");
        f.reorder(c, x, y).fuse(c, x, fused).vectorize(fused, 16);
    } else if (dst.stride(0) == 3) {
        // planar -> packed
        f.reorder(c, x, y).unroll(c).vectorize(x, 16);
    } else {
        // packed -> planar
        f.reorder(c, x, y).unroll(c).vectorize(x, 16);
    }

    f.realize(dst);

    return benchmark([&]() { return f.realize(dst); });
}

Buffer<uint8_t> make_packed(uint8_t *host, int W, int H) {
    return Buffer<uint8_t>::make_interleaved(host, W, H, 3);
}

Buffer<uint8_t> make_planar(uint8_t *host, int W, int H) {
    return Buffer<uint8_t>(host, W, H, 3);
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    const int W = 1 << 11, H = 1 << 11;

    // Allocate two 4 megapixel, 3 channel, 8-bit images -- input and output
    uint8_t *storage_1(new uint8_t[W * H * 3 + 32]);
    uint8_t *storage_2(new uint8_t[W * H * 3 + 32]);

    uint8_t *ptr_1 = storage_1, *ptr_2 = storage_2;
    while ((size_t)ptr_1 & 0x1f)
        ptr_1++;
    while ((size_t)ptr_2 & 0x1f)
        ptr_2++;

    double t_packed_packed = test_copy(make_packed(ptr_1, W, H),
                                       make_packed(ptr_2, W, H));
    double t_packed_planar = test_copy(make_packed(ptr_1, W, H),
                                       make_planar(ptr_2, W, H));
    double t_planar_packed = test_copy(make_planar(ptr_1, W, H),
                                       make_packed(ptr_2, W, H));
    double t_planar_planar = test_copy(make_planar(ptr_1, W, H),
                                       make_planar(ptr_2, W, H));

    delete[] storage_1;
    delete[] storage_2;

    if (t_planar_planar > t_packed_packed * 2 ||
        t_packed_packed > t_packed_planar * 2 ||
        t_planar_packed > t_packed_planar * 2) {
        printf("Times were not in expected order:\n"
               "planar -> planar: %f \n"
               "packed -> packed: %f \n"
               "planar -> packed: %f \n"
               "packed -> planar: %f \n",
               t_planar_planar,
               t_packed_packed,
               t_planar_packed,
               t_packed_planar);

        return 1;
    }

    printf("Success!\n");
    return 0;
}
