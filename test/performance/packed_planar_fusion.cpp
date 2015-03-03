#include "Halide.h"
#include <stdio.h>
#include "clock.h"
#include <memory>

using namespace Halide;

double test_copy(Image<uint8_t> src, Image<uint8_t> dst) {
    Var x, y, c;
    Func f;
    f(x, y, c) = src(x, y, c);

    for (int i = 0; i < 3; i++) {
        f.output_buffer()
            .set_stride(i, dst.stride(i))
            .set_extent(i, dst.extent(i))
            .set_min(i, dst.min(i));
    }

    if (dst.stride(0) == 1) {
        f.vectorize(x, 16);
    } else if (dst.stride(0) == 3 && src.stride(0) == 3) {
        // For a memcpy of packed rgb data, we can fuse x and c and
        // vectorize over the combination.
        Var fused("fused");
        f.reorder(c, x, y).fuse(c, x, fused).vectorize(fused, 16);
    }

    f.compile_to_assembly(std::string("copy_") + f.name() + ".s", Internal::vec<Argument>(src), "copy");

    f.realize(dst);



    double t1 = current_time();
    for (int i = 0; i < 10; i++) {
        f.realize(dst);
    }
    double t2 = current_time();
    return t2 - t1;
}

Image<uint8_t> make_packed(uint8_t *host, int W, int H) {
    buffer_t buf = {0};
    buf.host = host;
    buf.extent[0] = W;
    buf.stride[0] = 3;
    buf.extent[1] = H;
    buf.stride[1] = buf.stride[0] * buf.extent[0];
    buf.extent[2] = 3;
    buf.stride[2] = 1;
    buf.elem_size = 1;
    return Image<uint8_t>(&buf);
}

Image<uint8_t> make_planar(uint8_t *host, int W, int H) {
    buffer_t buf = {0};
    buf.host = host;
    buf.extent[0] = W;
    buf.stride[0] = 1;
    buf.extent[1] = H;
    buf.stride[1] = buf.stride[0] * buf.extent[0];
    buf.extent[2] = 3;
    buf.stride[2] = buf.stride[1] * buf.extent[1];
    buf.elem_size = 1;
    return Image<uint8_t>(&buf);
}

int main(int argc, char **argv) {

    const int W = 1<<11, H = 1<<11;

    // Allocate two 4 megapixel, 3 channel, 8-bit images -- input and output
    uint8_t *storage_1(new uint8_t[W * H * 3 + 32]);
    uint8_t *storage_2(new uint8_t[W * H * 3 + 32]);

    uint8_t *ptr_1 = storage_1, *ptr_2 = storage_2;
    while ((size_t)ptr_1 & 0x1f) ptr_1 ++;
    while ((size_t)ptr_2 & 0x1f) ptr_2 ++;

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

    if (t_planar_planar > t_packed_packed * 1.4 ||
        t_packed_packed > t_packed_planar * 1.4 ||
        t_packed_planar > t_planar_packed * 1.4) {
        printf("Times were not in expected order:\n"
               "planar -> planar: %f \n"
               "packed -> packed: %f \n"
               "packed -> planar: %f \n"
               "planar -> packed: %f \n",
               t_planar_planar,
               t_packed_packed,
               t_packed_planar,
               t_planar_packed);

        return -1;
    }




    printf("Success!\n");
    return 0;
}
