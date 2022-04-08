#include "Halide.h"
#include <algorithm>
#include <future>

#include <cstdio>

using namespace Halide;
using namespace Halide::BoundaryConditions;

Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

template<typename T>
bool expect_eq(T actual, T expected) {
    if (expected != actual) {
        fprintf(stderr, "Failed: expected %d, actual %d\n", (int)expected, (int)actual);
        return false;
    }
    return true;
}

void schedule_test(Func f, int vector_width, const Target &t) {
    if (vector_width != 1) {
        if (t.has_feature(Target::OpenGLCompute)) {
            // Vector stores not yet supported in OpenGLCompute backend
            f.unroll(x, vector_width);
        } else {
            f.vectorize(x, vector_width);
        }
    }
    if (t.has_gpu_feature() && vector_width <= 16) {
        f.gpu_tile(x, y, xo, yo, xi, yi, 2, 2);
    } else if (t.has_feature(Target::HVX)) {
        // TODO: Non-native vector widths hang the compiler here.
        // f.hexagon();
    }
}

template<typename T>
bool check_constant_exterior(const Buffer<T> &input, T exterior, Func f,
                             int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                             int vector_width,
                             Target t) {
    bool success = true;

    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, t);
    f.realize(result, t);
    result.copy_to_host();

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            if (x < 0 || y < 0 || x >= input.width() || y >= input.height()) {
                success &= expect_eq(result(x, y), exterior);
            } else {
                success &= expect_eq(result(x, y), input(x, y));
            }
        }
    }
    return success;
}

template<typename T>
bool check_repeat_edge(const Buffer<T> &input, Func f,
                       int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                       int vector_width,
                       Target t) {
    bool success = true;

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
            success &= expect_eq(result(x, y), input(clamped_x, clamped_y));
        }
    }
    return success;
}

template<typename T>
bool check_repeat_image(const Buffer<T> &input, Func f,
                        int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                        int vector_width,
                        Target t) {
    bool success = true;

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
            while (mapped_x < 0)
                mapped_x += input.width();
            while (mapped_x > input.width() - 1)
                mapped_x -= input.width();
            while (mapped_y < 0)
                mapped_y += input.height();
            while (mapped_y > input.height() - 1)
                mapped_y -= input.height();
            success &= expect_eq(result(x, y), input(mapped_x, mapped_y));
        }
    }
    return success;
}

template<typename T>
bool check_mirror_image(const Buffer<T> &input, Func f,
                        int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                        int vector_width,
                        Target t) {
    bool success = true;

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
            success &= expect_eq(result(x, y), input(mapped_x, mapped_y));
        }
    }
    return success;
}

template<typename T>
bool check_mirror_interior(const Buffer<T> &input, Func f,
                           int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                           int vector_width,
                           Target t) {
    bool success = true;

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

            success &= expect_eq(result(x, y), input(mapped_x, mapped_y));
        }
    }
    return success;
}

bool test_all(int vector_width, Target t) {
    bool success = true;

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
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        success &= check_repeat_edge(
            input,
            repeat_edge(input_f, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Image input.
        success &= check_repeat_edge(
            input,
            repeat_edge(input, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Undefined bounds.
        success &= check_repeat_edge(
            input,
            repeat_edge(input, {{Expr(), Expr()}, {0, H}}),
            0, W, test_min, test_extent,
            vector_width, t);
        success &= check_repeat_edge(
            input,
            repeat_edge(input, {{0, W}, {Expr(), Expr()}}),
            test_min, test_extent, 0, H,
            vector_width, t);
        // Implicitly determined bounds.
        success &= check_repeat_edge(
            input,
            repeat_edge(input),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
    }

    // constant_exterior:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        const uint8_t exterior = 42;

        // Func input.
        success &= check_constant_exterior(
            input, exterior,
            constant_exterior(input_f, exterior, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Image input.
        success &= check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Undefined bounds.
        success &= check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior, {{Expr(), Expr()}, {0, H}}),
            0, W, test_min, test_extent,
            vector_width, t);
        success &= check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior, {{0, W}, {Expr(), Expr()}}),
            test_min, test_extent, 0, H,
            vector_width, t);
        // Implicitly determined bounds.
        success &= check_constant_exterior(
            input, exterior,
            constant_exterior(input, exterior),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
    }

    // repeat_image:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        success &= check_repeat_image(
            input,
            repeat_image(input_f, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Image input.
        success &= check_repeat_image(
            input,
            repeat_image(input, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Undefined bounds.
        success &= check_repeat_image(
            input,
            repeat_image(input, {{Expr(), Expr()}, {0, H}}),
            0, W, test_min, test_extent,
            vector_width, t);
        success &= check_repeat_image(
            input,
            repeat_image(input, {{0, W}, {Expr(), Expr()}}),
            test_min, test_extent, 0, H,
            vector_width, t);
        // Implicitly determined bounds.
        success &= check_repeat_image(
            input,
            repeat_image(input),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
    }

    // mirror_image:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        success &= check_mirror_image(
            input,
            mirror_image(input_f, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Image input.
        success &= check_mirror_image(
            input,
            mirror_image(input, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Undefined bounds.
        success &= check_mirror_image(
            input,
            mirror_image(input, {{Expr(), Expr()}, {0, H}}),
            0, W, test_min, test_extent,
            vector_width, t);
        success &= check_mirror_image(
            input,
            mirror_image(input, {{0, W}, {Expr(), Expr()}}),
            test_min, test_extent, 0, H,
            vector_width, t);
        // Implicitly determined bounds.
        success &= check_mirror_image(
            input,
            mirror_image(input),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
    }

    // mirror_interior:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        success &= check_mirror_interior(
            input,
            mirror_interior(input_f, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Image input.
        success &= check_mirror_interior(
            input,
            mirror_interior(input, {{0, W}, {0, H}}),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
        // Undefined bounds.
        success &= check_mirror_interior(
            input,
            mirror_interior(input, {{Expr(), Expr()}, {0, H}}),
            0, W, test_min, test_extent,
            vector_width, t);
        success &= check_mirror_interior(
            input,
            mirror_interior(input, {{0, W}, {Expr(), Expr()}}),
            test_min, test_extent, 0, H,
            vector_width, t);
        // Implicitly determined bounds.
        success &= check_mirror_interior(
            input,
            mirror_interior(input),
            test_min, test_extent, test_min, test_extent,
            vector_width, t);
    }

    return success;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    Halide::Internal::ThreadPool<bool> pool;
    std::vector<std::future<bool>> futures;
    int vector_width_max = 32;
    if (target.has_feature(Target::Metal) ||
        target.has_feature(Target::OpenGLCompute) ||
        target.has_feature(Target::D3D12Compute)) {
        // https://github.com/halide/Halide/issues/2148
        vector_width_max = 4;
    }
    if (target.arch == Target::WebAssembly) {
        // The wasm jit is very slow, so shorten this test here.
        vector_width_max = 8;
    }
    for (int vector_width = 1; vector_width <= vector_width_max; vector_width *= 2) {
        std::cout << "Testing vector_width: " << vector_width << "\n";
        if (target.has_feature(Target::OpenGLCompute)) {
            // GL can't be used from multiple threads at once
            test_all(vector_width, target);
        } else {
            futures.push_back(pool.async(test_all, vector_width, target));
        }
    }

    bool success = true;
    for (auto &f : futures) {
        success &= f.get();
    }

    if (!success) {
        fprintf(stderr, "Failed!\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
