#include <algorithm>
#include <stdio.h>
#include "Halide.h"

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

    Func f, g, h, m, n;
    Func f_img, g_img, h_img, m_img, n_img;
    Func f_img_implicit, g_img_implicit, h_img_implicit, m_img_implicit, n_img_implicit;

    f(x, y) = constant_exterior(input_f, 42, 0, 10, 0, 10)(x, y);
    g(x, y) = repeat_edge(input_f, 0, 10, 0, 10)(x, y);
    h(x, y) = repeat_image(input_f, 0, 10, 0, 10)(x, y);
    m(x, y) = mirror_image(input_f, 0, 10, 0, 10)(x, y);
    n(x, y) = mirror_interior(input_f, 0, 10, 0, 10)(x, y);

    f_img(x, y) = constant_exterior(input, 42, 0, 10, 0, 10)(x, y);
    g_img(x, y) = repeat_edge(input, 0, 10, 0, 10)(x, y);
    h_img(x, y) = repeat_image(input, 0, 10, 0, 10)(x, y);
    m_img(x, y) = mirror_image(input, 0, 10, 0, 10)(x, y);
    n_img(x, y) = mirror_interior(input, 0, 10, 0, 10)(x, y);

    f_img_implicit(x, y) = constant_exterior(input, 42)(x, y);
    g_img_implicit(x, y) = repeat_edge(input)(x, y);
    h_img_implicit(x, y) = repeat_image(input)(x, y);
    m_img_implicit(x, y) = mirror_image(input)(x, y);
    n_img_implicit(x, y) = mirror_interior(input)(x, y);

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

    Image<uint8_t> result_constant_exterior_img(test_extent, test_extent);
    result_constant_exterior_img.set_min(test_min, test_min);
    f_img.realize(result_constant_exterior_img);

    for (int32_t i = test_extent; i < (test_min + test_extent); i++) {
        for (int32_t j = test_extent; j < (test_min + test_extent); j++) {
            if (i < 0 || j < 0 || i > 9 || j > 9) {
                assert(result_constant_exterior_img(i, j) == 42);
            } else {
                assert(result_constant_exterior_img(i, j) == input(i, j));
            }
        }
    }

    Image<uint8_t> result_constant_exterior_img_implicit(test_extent, test_extent);
    result_constant_exterior_img_implicit.set_min(test_min, test_min);
    f_img_implicit.realize(result_constant_exterior_img_implicit);

    for (int32_t i = test_extent; i < (test_min + test_extent); i++) {
        for (int32_t j = test_extent; j < (test_min + test_extent); j++) {
            if (i < 0 || j < 0 || i > 9 || j > 9) {
                assert(result_constant_exterior_img_implicit(i, j) == 42);
            } else {
                assert(result_constant_exterior_img_implicit(i, j) == input(i, j));
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

    Image<uint8_t> result_repeat_edge_img(20, 20);
    result_repeat_edge_img.set_min(-5, -5);
    g_img.realize(result_repeat_edge_img);

    for (int32_t i = -5; i < 15; i++) {
        for (int32_t j = -5; j < 15; j++) {
            int32_t clamped_i = std::min(9, std::max(0, i));
            int32_t clamped_j = std::min(9, std::max(0, j));
            assert(result_repeat_edge_img(i, j) == input(clamped_i, clamped_j));
        }
    }

    Image<uint8_t> result_repeat_edge_img_implicit(20, 20);
    result_repeat_edge_img_implicit.set_min(-5, -5);
    g_img_implicit.realize(result_repeat_edge_img_implicit);

    for (int32_t i = -5; i < 15; i++) {
        for (int32_t j = -5; j < 15; j++) {
            int32_t clamped_i = std::min(9, std::max(0, i));
            int32_t clamped_j = std::min(9, std::max(0, j));
            assert(result_repeat_edge_img_implicit(i, j) == input(clamped_i, clamped_j));
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

    Image<uint8_t> result_repeat_image_img(50, 50);
    result_repeat_image_img.set_min(-25, -25);
    h_img.realize(result_repeat_image_img);

    for (int32_t i = -25; i < 25; i++) {
        for (int32_t j = -25; j < 25; j++) {
            int32_t mapped_i = i;
            int32_t mapped_j = j;
            while (mapped_i < 0) mapped_i += 10;
            while (mapped_i > 9) mapped_i -= 10;
            while (mapped_j < 0) mapped_j += 10;
            while (mapped_j > 9) mapped_j -= 10;

            assert(result_repeat_image_img(i, j) == input(mapped_i, mapped_j));
        }
    }

    Image<uint8_t> result_repeat_image_img_implicit(50, 50);
    result_repeat_image_img_implicit.set_min(-25, -25);
    h_img_implicit.realize(result_repeat_image_img_implicit);

    for (int32_t i = -25; i < 25; i++) {
        for (int32_t j = -25; j < 25; j++) {
            int32_t mapped_i = i;
            int32_t mapped_j = j;
            while (mapped_i < 0) mapped_i += 10;
            while (mapped_i > 9) mapped_i -= 10;
            while (mapped_j < 0) mapped_j += 10;
            while (mapped_j > 9) mapped_j -= 10;

            assert(result_repeat_image_img_implicit(i, j) == input(mapped_i, mapped_j));
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

    Image<uint8_t> result_mirror_image_img(test_extent, test_extent);
    result_mirror_image_img.set_min(test_min, test_min);
    m_img.realize(result_mirror_image_img);

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

            assert(result_mirror_image_img(i, j) == input(mapped_i, mapped_j));
        }
    }

    Image<uint8_t> result_mirror_image_img_implicit(test_extent, test_extent);
    result_mirror_image_img_implicit.set_min(test_min, test_min);
    m_img_implicit.realize(result_mirror_image_img_implicit);

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

            assert(result_mirror_image_img_implicit(i, j) == input(mapped_i, mapped_j));
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

    Image<uint8_t> result_mirror_interior_img(test_extent, test_extent);
    result_mirror_interior_img.set_min(test_min, test_min);
    n.realize(result_mirror_interior_img);

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

            assert(result_mirror_interior_img(i, j) == input(mapped_i, mapped_j));
        }
    }

    Image<uint8_t> result_mirror_interior_img_implicit(test_extent, test_extent);
    result_mirror_interior_img_implicit.set_min(test_min, test_min);
    n.realize(result_mirror_interior_img_implicit);

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

            assert(result_mirror_interior_img_implicit(i, j) == input(mapped_i, mapped_j));
        }
    }

    printf("Success!\n");
    return 0;
}
