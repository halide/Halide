#include "Halide.h"
#include <cstdio>

#include "halide_benchmark.h"

const int W = 8000, H = 6000;

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::Tools;

Target target;

Buffer<float> make_replicated_buffer(int w, int h) {
    // Make a buffer that is just the same memory for every scanline,
    // to ensure it fits in L1/L2. We're just trying to measure
    // codegen effects here, not cache effects of different boundary
    // conditions.

    Buffer<float> buf(w, 1);
    buf.raw_buffer()->dim[1].extent = h;
    buf.raw_buffer()->dim[1].stride = 0;
    return buf;
}

struct Test {
    const char *name;
    Func f;
    double time;

    // Test a small stencil
    void test1() {
        Func g(name);
        Var x, y;
        g(x, y) = f(x - 1, y - 1) + f(x, y) + f(x + 1, y + 1);
        if (target.has_gpu_feature()) {
            Var xo, yo, xi, yi;
            g.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
        } else {
            g.vectorize(x, 4);
        }

        g.compile_jit();

        Buffer<float> out = make_replicated_buffer(W, H);
        time = benchmark([&]() {
            g.realize(out);
            out.device_sync();
        });

        printf("%-20s: %f us\n", name, time * 1e6);
    }

    // Test a larger stencil using an RDom
    void test2() {
        Param<int> blur_radius(2, 0, 10);

        Func g(name);
        Var x, y, xi, yi;
        RDom r(-blur_radius, 2 * blur_radius + 1, -blur_radius, 2 * blur_radius + 1);
        g(x, y) = sum(f(x + r.x, y + r.y));
        if (target.has_gpu_feature()) {
            Var xo, yo, xi, yi;
            g.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
        } else {
            g.tile(x, y, xi, yi, 8, 8).vectorize(xi, 4);
        }

        g.compile_jit();

        Buffer<float> out = make_replicated_buffer(W, H);
        time = benchmark([&]() {
            g.realize(out);
            out.device_sync();
        });

        printf("%-20s: %f us\n", name, time * 1e6);
    }
};

int main(int argc, char **argv) {
    target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    // Workaround for https://github.com/halide/Halide/issues/7420
    if (target.has_feature(Target::WebGPU)) {
        printf("[SKIP] workaround for issue #7420 (performance 2x as slow as expected)\n");
        return 0;
    }

    ImageParam input(Float(32), 2);
    ImageParam padded_input(Float(32), 2);

    // We use image params bound to concrete images. Using images
    // directly lets Halide assume things about the width and height,
    // and we don't want that to pollute the timings.
    Buffer<float> in = make_replicated_buffer(W, H);

    // A padded version of the input to use as a baseline.
    Buffer<float> padded_in = make_replicated_buffer(W + 16, H + 16);

    Var x, y;

    input.set(in);
    padded_input.set(padded_in);

    // Apply several different boundary conditions.
    Test tests[] = {
        {"unbounded", lambda(x, y, padded_input(x + 8, y + 8)), 0.0},
        {"constant_exterior", constant_exterior(input, 0.0f), 0.0},
        {"repeat_edge", repeat_edge(input), 0.0},
        {"repeat_image", repeat_image(input), 0.0},
        {"mirror_image", mirror_image(input), 0.0},
        {"mirror_interior", mirror_interior(input), 0.0},
        {nullptr, Func(), 0.0}};  // Sentinel

    // Time each
    for (int i = 0; tests[i].name; i++) {
        tests[i].test1();
        // Nothing should be that much more expensive than unbounded
        if (tests[i].time > tests[0].time * 5) {
            printf("Error: %s is %f times slower than unbounded\n",
                   tests[i].name, tests[i].time / tests[0].time);
            return 1;
        }
    }

    for (int i = 0; tests[i].name; i++) {
        tests[i].test2();
        // Nothing should be that much more expensive than unbounded
        if (tests[i].time > tests[0].time * 2) {
            printf("Error: %s is %f times slower than unbounded\n",
                   tests[i].name, tests[i].time / tests[0].time);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
