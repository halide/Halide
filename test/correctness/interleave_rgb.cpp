#include <stdio.h>
#include "Halide.h"

using namespace Halide;

template <typename T>
bool test_interleave() {
    Var x("x"), y("y"), c("c");

    Func input("input");
    input(x, y, c) = cast<T>(x * 3 + y * 5 + c);

    Func interleaved("interleaved");
    interleaved(x, y, c) = input(x, y, c);

    Target target = get_jit_target_from_environment();
    input.compute_root();
    interleaved.reorder(c, x, y).bound(c, 0, 3);
    interleaved.output_buffer().set_stride(0, 3).set_stride(2, 1).set_extent(2, 3);

    if (target.has_gpu_feature()) {
        interleaved.gpu_tile(x, y, 16, 16);
    } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        interleaved.hexagon().vectorize(x, 128 / sizeof(T)).unroll(c);
    } else {
        interleaved.vectorize(x, target.natural_vector_size<uint8_t>()).unroll(c);
    }
    Buffer buff(type_of<T>(), 256, 128, 3);
    buff.raw_buffer()->stride[0] = 3;
    buff.raw_buffer()->stride[1] = 3 * buff.extent(0);
    buff.raw_buffer()->stride[2] = 1;

    Realization r({buff});
    interleaved.realize(r, target);
    Image<T> out = r[0];
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            for (int c = 0; c < 3; c++) {
                T correct = x * 3 + y * 5 + c;
                if (out(x, y, c) != correct) {
                    printf("out(%d, %d, %d) = %d instead of %d\n", x, y, c, out(x, y, c), correct);
                    return false;
                }
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (!test_interleave<uint8_t>()) return -1;
    if (!test_interleave<uint16_t>()) return -1;

    printf("Success!\n");
    return 0;
}
