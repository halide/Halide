#include "Halide.h"
#include <stdio.h>

#include "testing.h"

using namespace Halide;

int test_per_channel_select() {

    printf("Testing select of channel.\n");

    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Func gpu("gpu"), cpu("cpu");
    Var x("x"), y("y"), c("c");

    gpu(x, y, c) = cast<uint8_t>(select(c == 0, 128,
                                        c == 1, x,
                                        c == 2, y,
                                        x * y));
    gpu.bound(c, 0, 4);
    gpu.glsl(x, y, c);
    gpu.compute_root();

    cpu(x, y, c) = gpu(x, y, c);

    Buffer<uint8_t> out(10, 10, 4);
    cpu.realize(out, target);

    // Verify the result
    if (!Testing::check_result<uint8_t>(out, [&](int x, int y, int c) {
	    switch (c) {
		case 0: return 128;
		case 1: return x;
		case 2: return y;
		default: return x*y;
	    } })) {
        return 1;
    }

    return 0;
}

int test_flag_scalar_select() {

    printf("Testing select of scalar value with flag.\n");

    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Func gpu("gpu"), cpu("cpu");
    Var x("x"), y("y"), c("c");

    int flag_value = 0;

    Param<int> flag("flag");
    flag.set(flag_value);

    gpu(x, y, c) = cast<uint8_t>(select(flag != 0, 128,
                                        255));
    gpu.bound(c, 0, 4);
    gpu.glsl(x, y, c);
    gpu.compute_root();

    // This should trigger a copy_to_host operation
    cpu(x, y, c) = gpu(x, y, c);

    Buffer<uint8_t> out(10, 10, 4);
    cpu.realize(out, target);

    // Verify the result
    if (!Testing::check_result<uint8_t>(out, [&](int x, int y, int c) {
            return !flag_value ? 255 : 128;
        })) {
        return 1;
    }

    return 0;
}

int test_flag_pixel_select() {

    printf("Testing select of pixel value with flag.\n");

    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Func gpu("gpu"), cpu("cpu");
    Var x("x"), y("y"), c("c");

    int flag_value = 0;

    Param<int> flag("flag");
    flag.set(flag_value);

    Buffer<uint8_t> image(10, 10, 4);
    for (int y = 0; y < image.height(); y++) {
        for (int x = 0; x < image.width(); x++) {
            for (int c = 0; c < image.channels(); c++) {
                image(x, y, c) = 128;
            }
        }
    }

    gpu(x, y, c) = cast<uint8_t>(select(flag != 0, image(x, y, c),
                                        255));
    gpu.bound(c, 0, 4);
    gpu.glsl(x, y, c);
    gpu.compute_root();

    // This should trigger a copy_to_host operation
    cpu(x, y, c) = gpu(x, y, c);

    Buffer<uint8_t> out(10, 10, 4);
    cpu.realize(out, target);

    // Verify the result
    if (!Testing::check_result<uint8_t>(out, [&](int x, int y, int c) {
            return !flag_value ? 255 : 128;
        })) {
        return 1;
    }

    return 0;
}

int test_nested_select() {

    printf("Testing nested select.\n");

    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    // Define the algorithm.
    Var x("x"), y("y"), c("c");
    Func f("f");
    Expr temp = cast<uint8_t>(select(x == 0, 1, 2));
    f(x, y, c) = select(y == 0, temp, 255 - temp);

    // Schedule f to run on the GPU.
    const int channels = 3;
    f.bound(c, 0, channels).glsl(x, y, c);

    // Generate the result.
    const int width = 10, height = 10;
    Buffer<uint8_t> out = f.realize(width, height, channels, target);

    // Check the result.
    int errors = 0;
    out.for_each_element([&](int x, int y, int c) {
        uint8_t temp = x == 0 ? 1 : 2;
        uint8_t expected = y == 0 ? temp : 255 - temp;
        uint8_t actual = out(x, y, c);
        if (expected != actual && ++errors == 1) {
            fprintf(stderr, "out(%d, %d, %d) = %d instead of %d\n",
                    x, y, c, actual, expected);
        }
    });

    return errors;
}

int test_nested_select_varying() {

    printf("Testing nested select with varying condition.\n");

    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    // Define the algorithm.
    Var x("x"), y("y"), c("c");
    Func f("f");
    Expr temp = cast<uint8_t>(select(x - c > 0, 1, 2));
    f(x, y, c) = select(y == 0, temp, 255 - temp);

    // Schedule f to run on the GPU.
    const int channels = 3;
    f.bound(c, 0, channels).glsl(x, y, c);

    // Generate the result.
    const int width = 10, height = 10;
    Buffer<uint8_t> out = f.realize(width, height, channels, target);

    // Check the result.
    int errors = 0;
    out.for_each_element([&](int x, int y, int c) {
        uint8_t temp = x - c > 0 ? 1 : 2;
        uint8_t expected = y == 0 ? temp : 255 - temp;
        uint8_t actual = out(x, y, c);
        if (expected != actual && ++errors == 1) {
            fprintf(stderr, "out(%d, %d, %d) = %d instead of %d\n",
                    x, y, c, actual, expected);
        }
    });

    return errors;
}

int main() {

    int err = 0;

    err |= test_per_channel_select();
    err |= test_flag_scalar_select();
    err |= test_flag_pixel_select();
    err |= test_nested_select();
    err |= test_nested_select_varying();

    if (err) {
        printf("FAILED\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
