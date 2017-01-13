#ifdef _WIN32
#include <stdio.h>
int main(int argc, char **argv) {
    printf("Skipping test on windows\n");
    return 0;
}
#else

#include <algorithm>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

bool failed = false;

template <typename T>
void expect_eq(T actual, T expected) {
    if (expected != actual) {
        fprintf(stderr, "Failed: expected %d, actual %d\n", (int) expected, (int) actual);
        exit(-1);
    }
}

void schedule_test(Func f, int vector_width, const Target &t) {
    if (vector_width != 1) {
        f.vectorize(x, vector_width);
    }
    if (t.has_gpu_feature() && vector_width <= 16) {
        f.gpu_tile(x, y, xo, yo, xi, yi, 2, 2);
    } else if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        // TODO: Non-native vector widths hang the compiler here.
        //f.hexagon();
    }
}

template <typename T>
void check_constant_exterior(const Buffer<T> &input, T exterior, Func f,
                             int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                             int vector_width,
                             Target t = get_jit_target_from_environment()) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, t);
    f.realize(result, t);
    result.copy_to_host();

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            if (x < 0 || y < 0 || x >= input.width() || y >= input.height()) {
                expect_eq(result(x, y), exterior);
            } else {
                expect_eq(result(x, y), input(x, y));
            }
        }
    }
}

template <typename T>
void check_repeat_edge(const Buffer<T> &input, Func f,
                       int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                       int vector_width,
                       Target t = get_jit_target_from_environment()) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, t);
    f.realize(result, t);
    result.copy_to_host();

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t clamped_y = std::min(input.height() - 1, std::max(0, y));
            int32_t clamped_x = std::min(input.width() - 1, std::max(0, x));
            expect_eq(result(x, y), input(clamped_x, clamped_y));
        }
    }
}

template <typename T>
void check_repeat_image(const Buffer<T> &input, Func f,
                        int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                        int vector_width,
                        Target t = get_jit_target_from_environment()) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, t);
    f.realize(result, t);
    result.copy_to_host();

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t mapped_x = x;
            int32_t mapped_y = y;
            while (mapped_x < 0) mapped_x += input.width();
            while (mapped_x > input.width() - 1) mapped_x -= input.width();
            while (mapped_y < 0) mapped_y += input.height();
            while (mapped_y > input.height() - 1) mapped_y -= input.height();
            expect_eq(result(x, y), input(mapped_x, mapped_y));
        }
    }
}

template <typename T>
void check_mirror_image(const Buffer<T> &input, Func f,
                        int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                        int vector_width,
                        Target t = get_jit_target_from_environment()) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, t);
    f.realize(result, t);
    result.copy_to_host();

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
            expect_eq(result(x, y), input(mapped_x, mapped_y));
        }
    }
}

template <typename T>
void check_mirror_interior(const Buffer<T> &input, Func f,
                           int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                           int vector_width,
                           Target t = get_jit_target_from_environment()) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, t);
    f.realize(result, t);
    result.copy_to_host();

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

            expect_eq(result(x, y), input(mapped_x, mapped_y));
        }
    }
}

void test_all(int vector_width) {

    const int W = 32;
    const int H = 32;
    Buffer<uint8_t> input(W, H);
    for (int32_t y = 0; y < H; y++) {
        for (int32_t x = 0; x < W; x++) {
            input(x, y) = x + y * W;
        }
    }

    Func input_f("input_f");
    input_f(x, y) = input(x, y);

    // repeat_edge:
    std::cout << "repeat_edge, vector width: " << vector_width << "\n";
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        check_repeat_edge(
            input,
            repeat_edge(input_f, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Image input.
        check_repeat_edge(
            input,
            repeat_edge(input, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Undefined bounds.
        check_repeat_edge(
            input,
            repeat_edge(input, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent,
            vector_width);
        check_repeat_edge(
            input,
            repeat_edge(input, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H,
            vector_width);
        // Implicitly determined bounds.
        check_repeat_edge(
            input,
            repeat_edge(input),
            test_min, test_extent, test_min, test_extent,
            vector_width);
    }

    // constant_exterior:
    std::cout << "constant_exterior, vector width: " << vector_width << "\n";
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        const uint8_t exterior = 42;

        // Func input.
        check_constant_exterior(
            input, exterior,
            constant_exterior(input_f, exterior, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Image input.
        check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Undefined bounds.
        check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent,
            vector_width);
        check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H,
            vector_width);
        // Implicitly determined bounds.
        check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior),
            test_min, test_extent, test_min, test_extent,
            vector_width);
    }

    // repeat_image:
    std::cout << "repeat_image, vector width: " << vector_width << "\n";
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        check_repeat_image(
            input,
            repeat_image(input_f, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Image input.
        check_repeat_image(
            input,
            repeat_image(input, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Undefined bounds.
        check_repeat_image(
            input,
            repeat_image(input, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent,
            vector_width);
        check_repeat_image(
            input,
            repeat_image(input, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H,
            vector_width);
        // Implicitly determined bounds.
        check_repeat_image(
            input,
            repeat_image(input),
            test_min, test_extent, test_min, test_extent,
            vector_width);
    }

    // mirror_image:
    std::cout << "mirror_image, vector width: " << vector_width << "\n";
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        check_mirror_image(
            input,
            mirror_image(input_f, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Image input.
        check_mirror_image(
            input,
            mirror_image(input, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Undefined bounds.
        check_mirror_image(
            input,
            mirror_image(input, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent,
            vector_width);
        check_mirror_image(
            input,
            mirror_image(input, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H,
            vector_width);
        // Implicitly determined bounds.
        check_mirror_image(
            input,
            mirror_image(input),
            test_min, test_extent, test_min, test_extent,
            vector_width);
    }

    // mirror_interior:
    std::cout << "mirror_interior, vector width: " << vector_width << "\n";
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        check_mirror_interior(
            input,
            mirror_interior(input_f, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Image input.
        check_mirror_interior(
            input,
            mirror_interior(input, 0, W, 0, H),
            test_min, test_extent, test_min, test_extent,
            vector_width);
        // Undefined bounds.
        check_mirror_interior(
            input,
            mirror_interior(input, Expr(), Expr(), 0, H),
            0, W, test_min, test_extent,
            vector_width);
        check_mirror_interior(
            input,
            mirror_interior(input, 0, W, Expr(), Expr()),
            test_min, test_extent, 0, H,
            vector_width);
        // Implicitly determined bounds.
        check_mirror_interior(
            input,
            mirror_interior(input),
            test_min, test_extent, test_min, test_extent,
            vector_width);
    }
}

int main(int argc, char **argv) {

    std::vector<int> children;
    for (int vector_width = 1; vector_width <= 32; vector_width *= 2) {
        int pid = fork();
        if (!pid) {
            // I'm a worker
            test_all(vector_width);
            return 0;
        }
        // I'm the master
        children.push_back(pid);
    }

    // Wait for any children to terminate
    for (int child : children) {
        int child_status = 0;
        waitpid(child, &child_status, 0);
        if (child_status) {
            failed = true;
        }
    }

    if (!children.empty() && !failed) {
        printf("Success!\n");
    }

    return failed ? -1 : 0;
}
#endif
