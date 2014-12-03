#include <Halide.h>
#include <stdio.h>
#include "clock.h"

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam src(UInt(8), 1);
    Func dst;
    Var x;
    dst(x) = src(x);


    Var xo;
    dst.split(x, xo, x, 8*4096);
    // dst.parallel(xo); speeds up halide's memcpy considerably, but doesn't seem sporting
    dst.vectorize(x, 16);

    dst.compile_to_assembly("memcpy.s", Internal::vec<Argument>(src), "memcpy");
    dst.compile_jit();

    const int32_t buffer_size = 12345678;
    const int iterations = 50;

    Image<uint8_t> input(buffer_size);
    Image<uint8_t> output(buffer_size);

    src.set(input);

    // Get past one-time set-up issues for the ptx backend.
    dst.realize(output);

    double halide = 0, system = 0;
    for (int i = 0; i < iterations; i++) {
        double t1 = current_time();
        dst.realize(output);
        dst.realize(output);
        dst.realize(output);
        double t2 = current_time();
        memcpy(output.data(), input.data(), input.width());
        memcpy(output.data(), input.data(), input.width());
        memcpy(output.data(), input.data(), input.width());
        double t3 = current_time();
        system += t3-t2;
        halide += t2-t1;
    }

    printf("system memcpy: %.3e byte/s\n", (buffer_size / system) * 3 * 1000 * iterations);
    printf("halide memcpy: %.3e byte/s\n", (buffer_size / halide) * 3 * 1000 * iterations);

    // memcpy will win by a little bit for large inputs because it uses streaming stores
    if (halide > system * 2) {
        printf("Halide memcpy is slower than it should be.\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
