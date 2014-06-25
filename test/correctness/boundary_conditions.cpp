#include <algorithm>
#include <stdio.h>
#include <Halide.h>

using namespace Halide;
using namespace Halide::BoundaryConditions;

int main(int argc, char **argv) {

    Image<uint8_t> input(10, 10);

    for (int32_t i = 0; i < 10; i++) {
        for (int32_t j = 0; j < 10; j++) {
          input(i, j) = i + j * 10;
        }
    }

    Var x("x"), y("y");
    Func input_f("input_f");

    input_f(x, y) = input(x, y);

    Func f("f"), g("g"), h("h"), m("m"), n("n");

    f(x, y) = constant_exterior(input_f, 42, 0, 10, 0, 10)(x, y);
    g(x, y) = repeat_edge(input_f, 0, 10, 0, 10)(x, y);
    h(x, y) = repeat_image(input_f, 0, 10, 0, 10)(x, y);
    m(x, y) = mirror_image(input_f, 0, 10, 0, 10)(x, y);
    n(x, y) = mirror_interior(input_f, 0, 10, 0, 10)(x, y);

    const int32_t test_min = -25;
    const int32_t test_extent = 50;

    Image<uint8_t> result_constant_exterior(test_extent, test_extent);
    result_constant_exterior.set_min(test_min, test_min);
    f.realize(result_constant_exterior);

    for (int32_t i = test_extent; i < (test_min + test_extent); i++) {
        for (int32_t j = test_extent; j < (test_min + test_extent); j++) {
            if (i < 0 || j < 0 || i > 9 || j > 9) {
                assert(result_constant_exterior(i, j) == 42);
            } else {
                assert(result_constant_exterior(i, j) == input(i, j));
            }
        }
    }

    Image<uint8_t> result_repeat_edge(20, 20);
    result_repeat_edge.set_min(-5, -5);
    g.realize(result_repeat_edge);

    for (int32_t i = -5; i < 15; i++) {
        for (int32_t j = -5; j < 15; j++) {
            int32_t clamped_i = std::min(9, std::max(0, i));
            int32_t clamped_j = std::min(9, std::max(0, j));
            assert(result_repeat_edge(i, j) == input(clamped_i, clamped_j));
        }
    }

    Image<uint8_t> result_repeat_image(50, 50);
    result_repeat_image.set_min(-25, -25);
    h.realize(result_repeat_image);

    for (int32_t i = -25; i < 25; i++) {
        for (int32_t j = -25; j < 25; j++) {
            int32_t mapped_i = i;
            int32_t mapped_j = j;
            while (mapped_i < 0) mapped_i += 10;
            while (mapped_i > 9) mapped_i -= 10;
            while (mapped_j < 0) mapped_j += 10;
            while (mapped_j > 9) mapped_j -= 10;

            assert(result_repeat_image(i, j) == input(mapped_i, mapped_j));
        }
    }

    Image<uint8_t> result_mirror_image(test_extent, test_extent);
    result_mirror_image.set_min(test_min, test_min);
    m.realize(result_mirror_image);

    for (int32_t j = test_min; j < (test_min + test_extent); j++) {
        for (int32_t i = test_min; i < (test_min + test_extent); i++) {
            int32_t mapped_i = (i < 0) ? -(i + 1) : i;
            mapped_i = mapped_i % (2 * 10);
            if (mapped_i > (10 - 1)) {
                mapped_i = (2 * 10 - 1) - mapped_i;
            }
            int32_t mapped_j = (j < 0) ? -(j + 1) : j;
            mapped_j = mapped_j % (2 * 10);
            if (mapped_j > (10 - 1)) {
                mapped_j = (2 * 10 - 1) - mapped_j;
            }

            assert(result_mirror_image(i, j) == input(mapped_i, mapped_j));
        }
    }

    Image<uint8_t> result_mirror_interior(test_extent, test_extent);
    result_mirror_interior.set_min(test_min, test_min);
    n.realize(result_mirror_interior);

    for (int32_t j = test_min; j < (test_min + test_extent); j++) {
        for (int32_t i = test_min; i < (test_min + test_extent); i++) {
            int32_t mapped_i = abs(i) % 18;
            if (mapped_i > 9) {
                mapped_i = 18 - mapped_i;
            }
            int32_t mapped_j = abs(j) % 18;
            if (mapped_j > 9) {
                mapped_j = 18 - mapped_j;
            }

            assert(result_mirror_interior(i, j) == input(mapped_i, mapped_j));
        }
    }

    printf("Success!\n");
    return 0;
}
