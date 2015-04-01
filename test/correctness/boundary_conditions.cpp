#include <algorithm>
#include <stdio.h>
#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

template <typename T>
void check_constant_exterior(const Image<T> &input, T exterior, Func f,
                             int test_min_x, int test_extent_x, int test_min_y, int test_extent_y) {
    Image<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f.realize(result);

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            if (x < 0 || y < 0 || x >= input.width() || y >= input.height()) {
                assert(result(x, y) == exterior);
            } else {
                assert(result(x, y) == input(x, y));
            }
        }
    }
}

template <typename T>
void check_repeat_edge(const Image<T> &input, Func f,
                       int test_min_x, int test_extent_x, int test_min_y, int test_extent_y) {
    Image<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f.realize(result);

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t clamped_y = std::min(input.height() - 1, std::max(0, y));
            int32_t clamped_x = std::min(input.width() - 1, std::max(0, x));
            assert(result(x, y) == input(clamped_x, clamped_y));
        }
    }
}

template <typename T>
void check_repeat_image(const Image<T> &input, Func f,
                        int test_min_x, int test_extent_x, int test_min_y, int test_extent_y) {
    Image<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f.realize(result);

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t mapped_x = x;
            int32_t mapped_y = y;
            while (mapped_x < 0) mapped_x += input.width();
            while (mapped_x > input.width() - 1) mapped_x -= input.width();
            while (mapped_y < 0) mapped_y += input.height();
            while (mapped_y > input.height() - 1) mapped_y -= input.height();
            assert(result(x, y) == input(mapped_x, mapped_y));
        }
    }
}

template <typename T>
void check_mirror_image(const Image<T> &input, Func f,
                        int test_min_x, int test_extent_x, int test_min_y, int test_extent_y) {
    Image<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f.realize(result);

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t mapped_x = (x < 0) ? -(x + 1) : x;
            mapped_x = mapped_x % (2 * input.width());
            if (mapped_x > (input.width() - 1)) {
                mapped_x = (2 * input.width() - 1) - mapped_x;
            }
            int32_t mapped_y = (y < 0) ? -(y + 1) : y;
            mapped_y = mapped_y % (2 * input.height());
            if (mapped_y > (input.height() - 1)) {
                mapped_y = (2 * input.height() - 1) - mapped_y;
            }
            assert(result(x, y) == input(mapped_x, mapped_y));
        }
    }
}

template <typename T>
void check_mirror_interior(const Image<T> &input, Func f,
                           int test_min_x, int test_extent_x, int test_min_y, int test_extent_y) {
    Image<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f.realize(result);

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t mapped_x = abs(x) % (input.width() * 2 - 2);
            if (mapped_x > input.width() - 1) {
                mapped_x = input.width() * 2 - 2 - mapped_x;
            }
            int32_t mapped_y = abs(y) % (input.height() * 2 - 2);
            if (mapped_y > input.height() - 1) {
                mapped_y = input.height() * 2 - 2 - mapped_y;
            }

            assert(result(x, y) == input(mapped_x, mapped_y));
        }
    }
}

int main(int argc, char **argv) {

    const int W = 10;
    const int H = 10;
    Image<uint8_t> input(W, H);

    for (int32_t y = 0; y < H; y++) {
        for (int32_t x = 0; x < W; x++) {
          input(x, y) = x + y * W;
        }
    }

    Var x("x"), y("y");
    Func input_f("input_f");
    input_f(x, y) = input(x, y);

    // constant_exterior:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 50;

        const uint8_t exterior = 42;

        // Func input.
        check_constant_exterior(
            input, exterior,
            constant_exterior(input_f, exterior, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Image input.
        check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Undefined bounds.
        check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent);
        check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H);
        // Implicitly determined bounds.
        check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior),
            test_min, test_extent, test_min, test_extent);
    }

    // repeat_edge:
    {
        const int32_t test_min = -5;
        const int32_t test_extent = 20;

        // Func input.
        check_repeat_edge(
            input,
            repeat_edge(input_f, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Image input.
        check_repeat_edge(
            input,
            repeat_edge(input, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Undefined bounds.
        check_repeat_edge(
            input,
            repeat_edge(input, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent);
        check_repeat_edge(
            input,
            repeat_edge(input, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H);
        // Implicitly determined bounds.
        check_repeat_edge(
            input,
            repeat_edge(input),
            test_min, test_extent, test_min, test_extent);
    }

    // repeat_image:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 50;

        // Func input.
        check_repeat_image(
            input,
            repeat_image(input_f, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Image input.
        check_repeat_image(
            input,
            repeat_image(input, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Undefined bounds.
        check_repeat_image(
            input,
            repeat_image(input, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent);
        check_repeat_image(
            input,
            repeat_image(input, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H);
        // Implicitly determined bounds.
        check_repeat_image(
            input,
            repeat_image(input),
            test_min, test_extent, test_min, test_extent);
    }

    // mirror_image:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 50;

        // Func input.
        check_mirror_image(
            input,
            mirror_image(input_f, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Image input.
        check_mirror_image(
            input,
            mirror_image(input, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Undefined bounds.
        check_mirror_image(
            input,
            mirror_image(input, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent);
        check_mirror_image(
            input,
            mirror_image(input, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H);
        // Implicitly determined bounds.
        check_mirror_image(
            input,
            mirror_image(input),
            test_min, test_extent, test_min, test_extent);
    }

    // mirror_interior:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 50;

        // Func input.
        check_mirror_interior(
            input,
            mirror_interior(input_f, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Image input.
        check_mirror_interior(
            input,
            mirror_interior(input, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent);
        // Undefined bounds.
        check_mirror_interior(
            input,
            mirror_interior(input, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent);
        check_mirror_interior(
            input,
            mirror_interior(input, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H);
        // Implicitly determined bounds.
        check_mirror_interior(
            input,
            mirror_interior(input),
            test_min, test_extent, test_min, test_extent);
    }

    printf("Success!\n");
    return 0;
}
