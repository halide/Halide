#include "Halide.h"
#include <stdio.h>

#include "clock.h"

const int W = 1024, H = 768;

using namespace Halide;
using namespace Halide::BoundaryConditions;

struct Test {
    const char *name;
    Func f;
    double time;

    void test() {
        Func g(name);
        Var x, y;
        g(x, y) = f(x - 1, y - 1) + f(x, y) + f(x + 1, y + 1);
        g.vectorize(x, 4);
        g.compile_jit();

        Image<float> out = g.realize(W, H);

        // Best of 5
        for (int i = 0; i < 5; i++) {
            double t1 = current_time();
            // 10 runs
            for (int j = 0; j < 10; j++) {
                g.realize(out);
            }
            double t2 = current_time();
            double delta = t2 - t1;
            if (delta < time || i == 0) {
                time = delta;
            }
        }

        printf("%s: %f\n", name, time);
    }
};

int main(int argc, char **argv) {
    ImageParam input(Float(32), 2);
    ImageParam padded_input(Float(32), 2);

    // We use image params bound to concrete images. Using images
    // directly lets Halide assume things about the width and height,
    // and we don't want that to pollute the timings.
    Image<float> in(W, H);

    // A padded version of the input to use as a baseline.
    Image<float> padded_in(W + 8, H + 2);

    Var x, y;

    input.set(in);
    padded_input.set(padded_in);

    // Apply several different boundary conditions.
    Test tests[] = {
        {"unbounded", lambda(x, y, padded_input(x+1, y+1)), 0.0},
        {"constant_exterior", constant_exterior(input, 0.0f), 0.0},
        {"repeat_edge", repeat_edge(input), 0.0},
        {"repeat_image", repeat_image(input), 0.0},
        {"mirror_image", mirror_image(input), 0.0},
        {"mirror_interior", mirror_interior(input), 0.0},
        {NULL, Func(), 0.0}}; // Sentinel

    // Time each
    for (int i = 0; tests[i].name; i++) {
        tests[i].test();
        // Nothing should be that much more expensive than unbounded
        if (tests[i].time > tests[0].time * 5) {
            printf("Error: %s is %f times slower than unbounded\n",
                   tests[i].name, tests[i].time / tests[0].time);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
