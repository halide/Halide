#include "Halide.h"
#include <stdio.h>

using namespace Halide;

template<typename T>
bool test_interleave(int x_stride) {
    Var x("x"), y("y"), c("c");

    Func input("input");
    input(x, y, c) = cast<T>(x * 3 + y * 5 + c);

    Func interleaved("interleaved");
    interleaved(x, y, c) = input(x, y, c);

    Target target = get_jit_target_from_environment();
    input.compute_root();
    interleaved.reorder(c, x, y).bound(c, 0, 3);
    interleaved.output_buffer()
        .dim(0)
        .set_stride(x_stride)
        .dim(2)
        .set_stride(1)
        .set_extent(3);

    if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi");
        interleaved.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        const int vector_width = 128 / sizeof(T);
        interleaved.hexagon().vectorize(x, vector_width, TailStrategy::GuardWithIf).unroll(c);
    } else {
        interleaved.vectorize(x, target.natural_vector_size<uint8_t>(), TailStrategy::GuardWithIf).unroll(c);
    }
    // Test that the extra channels aren't written to by filling the buffer with
    // a value and cropping it.
    Buffer<T> buff = Buffer<T>::make_interleaved(255, 128, x_stride);
    buff.fill(7);
    if (target.has_gpu_feature() || target.has_feature(Target::HVX)) {
        buff.copy_to_device(target);
    }
    Buffer<T> buff_cropped = buff;
    buff_cropped.crop(2, 0, 3);
    interleaved.realize(buff_cropped, target);
    buff.copy_to_host();
    for (int y = 0; y < buff.height(); y++) {
        for (int x = 0; x < buff.width(); x++) {
            for (int c = 0; c < x_stride; c++) {
                T correct = c < 3 ? x * 3 + y * 5 + c : 7;
                if (buff(x, y, c) != correct) {
                    printf("out(%d, %d, %d) = %d instead of %d\n", x, y, c, buff(x, y, c), correct);
                    return false;
                }
            }
        }
    }
    return true;
}

template<typename T>
bool test_deinterleave(int x_stride) {
    Var x("x"), y("y"), c("c");

    ImageParam input(halide_type_of<T>(), 3);
    input.dim(0).set_stride(x_stride);
    input.dim(2).set_min(0).set_extent(3).set_stride(1);

    Func deinterleaved("deinterleaved");
    deinterleaved(x, y, c) = input(x, y, c);

    Target target = get_jit_target_from_environment();
    deinterleaved.reorder(c, x, y).bound(c, 0, 3);

    if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi");
        deinterleaved.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        const int vector_width = 128 / sizeof(T);
        deinterleaved.hexagon().vectorize(x, vector_width, TailStrategy::GuardWithIf).unroll(c);
    } else {
        deinterleaved.vectorize(x, target.natural_vector_size<uint8_t>(), TailStrategy::GuardWithIf).unroll(c);
    }
    Buffer<T> input_buf = Buffer<T>::make_interleaved(255, 128, x_stride);
    input_buf.fill([]() { return rand(); });
    input_buf.crop(2, 0, 3);
    input.set(input_buf);

    Buffer<T> buff(255, 128, 3);
    deinterleaved.realize(buff, target);
    buff.copy_to_host();
    for (int y = 0; y < buff.height(); y++) {
        for (int x = 0; x < buff.width(); x++) {
            for (int c = 0; c < 3; c++) {
                T correct = input_buf(x, y, c);
                if (buff(x, y, c) != correct) {
                    printf("out(%d, %d, %d) = %d instead of %d\n", x, y, c, buff(x, y, c), correct);
                    return false;
                }
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    for (int x_stride : {3, 4}) {
        if (!test_interleave<uint8_t>(x_stride)) return 1;
        if (!test_interleave<uint16_t>(x_stride)) return 1;
        if (!test_interleave<uint32_t>(x_stride)) return 1;

        if (!test_deinterleave<uint8_t>(x_stride)) return 1;
        if (!test_deinterleave<uint16_t>(x_stride)) return 1;
        if (!test_deinterleave<uint32_t>(x_stride)) return 1;
    }

    printf("Success!\n");
    return 0;
}
