#include <Halide.h>
#include <stdio.h>
#include <stdlib.h>

#include <functional>

using namespace Halide;



int test_per_channel_select() {

    Func gpu("gpu"), cpu("cpu");
    Var x("x"), y("y"), c("c");

    gpu(x, y, c) = cast<uint8_t>(select(c == 0, 128,
                                        c == 1, x,
                                        c == 2, y,
                                        x*y));
    gpu.bound(c, 0, 4);
    gpu.glsl(x, y, c);
    gpu.compute_root();

    cpu(x, y, c) = gpu(x, y, c);

    Image<uint8_t> out(10, 10, 4);
    cpu.realize(out);

    // Verify the result
    for (int y=0; y!=out.height(); ++y) {
        for (int x=0; x!=out.width(); ++x) {
            for (int c=0; c!=out.channels(); ++c) {
                uint8_t expected;
                switch (c) {
                    case 0: expected = 128; break;
                    case 1: expected = x; break;
                    case 2: expected = y; break;
                    default: expected = x*y; break;
                }
                uint8_t actual  = out(x,y,c);

                if (expected != actual) {
                    fprintf(stderr, "Incorrect pixel (%d, %d, %d, %d) at x=%d y=%d.\n",
                            out(x, y, 0), out(x, y, 1), out(x, y, 2), out(x, y, 3),
                            x, y);
                    return 1;
                }
            }
        }
    }

    return 0;
}

int test_flag_scalar_select() {

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

    Image<uint8_t> out(10, 10, 4);
    cpu.realize(out);

    // Verify the result
    for (int y=0; y!=out.height(); ++y) {
        for (int x=0; x!=out.width(); ++x) {
            for (int c=0; c!=out.channels(); ++c) {
                uint8_t expected = !flag_value ? 255 : 128;
                uint8_t actual  = out(x,y,c);

                if (expected != actual) {
                    fprintf(stderr, "Incorrect pixel (%d, %d, %d, %d) at x=%d y=%d.\n",
                            out(x, y, 0), out(x, y, 1), out(x, y, 2), out(x, y, 3),
                            x, y);
                    return 1;
                }
            }
        }
    }

    return 0;
}

int test_flag_pixel_select() {

    Func gpu("gpu"), cpu("cpu");
    Var x("x"), y("y"), c("c");

    int flag_value = 0;

    Param<int> flag("flag");
    flag.set(flag_value);

    Image<uint8_t> image(10, 10, 4);
    for (int y=0; y<image.height(); y++) {
        for (int x=0; x<image.width(); x++) {
            for (int c=0; c<image.channels(); c++) {
                image(x, y, c) = 128;
            }
        }
    }

    gpu(x, y, c) = cast<uint8_t>(select(flag != 0, image(x,y,c),
                                        255));
    gpu.bound(c, 0, 4);
    gpu.glsl(x, y, c);
    gpu.compute_root();

    // This should trigger a copy_to_host operation
    cpu(x, y, c) = gpu(x, y, c);

    Image<uint8_t> out(10, 10, 4);
    cpu.realize(out);

    // Verify the result
    for (int y=0; y!=out.height(); ++y) {
        for (int x=0; x!=out.width(); ++x) {
            for (int c=0; c!=out.channels(); ++c) {
                uint8_t expected = !flag_value ? 255 : 128;
                uint8_t actual  = out(x,y,c);

                if (expected != actual) {
                    fprintf(stderr, "Incorrect pixel (%d, %d, %d, %d) at x=%d y=%d.\n",
                            out(x, y, 0), out(x, y, 1), out(x, y, 2), out(x, y, 3),
                            x, y);
                    return 1;
                }
            }
        }
    }

    return 0;
}



int main() {

    // This test must be run with an OpenGL target
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL))  {
        fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    int err = 0;

    err |= test_per_channel_select();
    err |= test_flag_scalar_select();
    err |= test_flag_pixel_select();

    if (err) {
        printf("FAILED\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
