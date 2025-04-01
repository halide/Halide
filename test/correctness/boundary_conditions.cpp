#include "Halide.h"
#include "test_sharding.h"

#include <algorithm>
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

void schedule_test(Func f, int vector_width, Partition partition_policy, const Target &t) {
    if (vector_width != 1) {
        f.vectorize(x, vector_width);
    }
    f.partition(x, partition_policy);
    f.partition(y, partition_policy);
    if (t.has_gpu_feature()) {
        f.gpu_tile(x, y, xo, yo, xi, yi, 2, 2);
    } else if (t.has_feature(Target::HVX)) {
        // TODO: Non-native vector widths hang the compiler here.
        // f.hexagon();
    }
}

template<typename T>
bool check_constant_exterior(const Buffer<T> &input, T exterior, Func f,
                             int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                             int vector_width, Partition partition_policy,
                             Target t) {
    bool success = true;

    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy, t);
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
                       int vector_width, Partition partition_policy,
                       Target t) {
    bool success = true;

    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy, t);
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
                        int vector_width, Partition partition_policy,
                        Target t) {
    bool success = true;

    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy, t);
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
                        int vector_width, Partition partition_policy,
                        Target t) {
    bool success = true;

    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy, t);
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
                           int vector_width, Partition partition_policy,
                           Target t) {
    bool success = true;

    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy, t);
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

struct Task {
    std::function<bool()> fn;
};

void add_all(int vector_width, Partition partition_policy, Target t, std::vector<Task> &tasks) {
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
        tasks.push_back({[=]() { return check_repeat_edge(
                                     input,
                                     repeat_edge(input_f, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Image input.
        tasks.push_back({[=]() { return check_repeat_edge(
                                     input,
                                     repeat_edge(input, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Undefined bounds.
        tasks.push_back({[=]() { return check_repeat_edge(
                                     input,
                                     repeat_edge(input, {{Expr(), Expr()}, {0, H}}),
                                     0, W, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        tasks.push_back({[=]() { return check_repeat_edge(
                                     input,
                                     repeat_edge(input, {{0, W}, {Expr(), Expr()}}),
                                     test_min, test_extent, 0, H,
                                     vector_width, partition_policy, t); }});
        // Implicitly determined bounds.
        tasks.push_back({[=]() { return check_repeat_edge(
                                     input,
                                     repeat_edge(input),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
    }

    // constant_exterior:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        const uint8_t exterior = 42;

        // Func input.
        tasks.push_back({[=]() { return check_constant_exterior(
                                     input, exterior,
                                     constant_exterior(input_f, exterior, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Image input.
        tasks.push_back({[=]() { return check_constant_exterior(
                                     input, exterior,
                                     constant_exterior(input, exterior, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Undefined bounds.
        tasks.push_back({[=]() { return check_constant_exterior(
                                     input, exterior,
                                     constant_exterior(input, exterior, {{Expr(), Expr()}, {0, H}}),
                                     0, W, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        tasks.push_back({[=]() { return check_constant_exterior(
                                     input, exterior,
                                     constant_exterior(input, exterior, {{0, W}, {Expr(), Expr()}}),
                                     test_min, test_extent, 0, H,
                                     vector_width, partition_policy, t); }});
        // Implicitly determined bounds.
        tasks.push_back({[=]() { return check_constant_exterior(
                                     input, exterior,
                                     constant_exterior(input, exterior),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
    }

    // repeat_image:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        tasks.push_back({[=]() { return check_repeat_image(
                                     input,
                                     repeat_image(input_f, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Image input.
        tasks.push_back({[=]() { return check_repeat_image(
                                     input,
                                     repeat_image(input, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Undefined bounds.
        tasks.push_back({[=]() { return check_repeat_image(
                                     input,
                                     repeat_image(input, {{Expr(), Expr()}, {0, H}}),
                                     0, W, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        tasks.push_back({[=]() { return check_repeat_image(
                                     input,
                                     repeat_image(input, {{0, W}, {Expr(), Expr()}}),
                                     test_min, test_extent, 0, H,
                                     vector_width, partition_policy, t); }});
        // Implicitly determined bounds.
        tasks.push_back({[=]() { return check_repeat_image(
                                     input,
                                     repeat_image(input),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
    }

    // mirror_image:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        tasks.push_back({[=]() { return check_mirror_image(
                                     input,
                                     mirror_image(input_f, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Image input.
        tasks.push_back({[=]() { return check_mirror_image(
                                     input,
                                     mirror_image(input, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Undefined bounds.
        tasks.push_back({[=]() { return check_mirror_image(
                                     input,
                                     mirror_image(input, {{Expr(), Expr()}, {0, H}}),
                                     0, W, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        tasks.push_back({[=]() { return check_mirror_image(
                                     input,
                                     mirror_image(input, {{0, W}, {Expr(), Expr()}}),
                                     test_min, test_extent, 0, H,
                                     vector_width, partition_policy, t); }});
        // Implicitly determined bounds.
        tasks.push_back({[=]() { return check_mirror_image(
                                     input,
                                     mirror_image(input),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
    }

    // mirror_interior:
    {
        const int32_t test_min = -25;
        const int32_t test_extent = 100;

        // Func input.
        tasks.push_back({[=]() { return check_mirror_interior(
                                     input,
                                     mirror_interior(input_f, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Image input.
        tasks.push_back({[=]() { return check_mirror_interior(
                                     input,
                                     mirror_interior(input, {{0, W}, {0, H}}),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        // Undefined bounds.
        tasks.push_back({[=]() { return check_mirror_interior(
                                     input,
                                     mirror_interior(input, {{Expr(), Expr()}, {0, H}}),
                                     0, W, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
        tasks.push_back({[=]() { return check_mirror_interior(
                                     input,
                                     mirror_interior(input, {{0, W}, {Expr(), Expr()}}),
                                     test_min, test_extent, 0, H,
                                     vector_width, partition_policy, t); }});
        // Implicitly determined bounds.
        tasks.push_back({[=]() { return check_mirror_interior(
                                     input,
                                     mirror_interior(input),
                                     test_min, test_extent, test_min, test_extent,
                                     vector_width, partition_policy, t); }});
    }
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    int vector_width_max = 32;
    if (target.has_feature(Target::Metal) ||
        target.has_feature(Target::Vulkan) ||
        target.has_feature(Target::D3D12Compute) ||
        target.has_feature(Target::WebGPU)) {
        // https://github.com/halide/Halide/issues/2148
        vector_width_max = 4;
    }
    if (target.has_feature(Target::OpenCL)) {
        vector_width_max = 16;
    }
    if (target.arch == Target::WebAssembly) {
        // The wasm jit is very slow, so shorten this test here.
        vector_width_max = 8;
    }

    std::vector<Task> tasks;
    for (int vector_width = 1; vector_width <= vector_width_max; vector_width *= 2) {
        add_all(vector_width, Partition::Auto, target, tasks);
        add_all(vector_width, Partition::Never, target, tasks);
    }

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        if (!task.fn()) {
            exit(1);
        }
    }

    printf("Success!\n");
    return 0;
}
