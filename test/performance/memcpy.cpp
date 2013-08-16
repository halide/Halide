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

    Image<uint8_t> input(12345678);
    Image<uint8_t> output(12345678);

    src.set(input);

    // Get past one-time set-up issues for the ptx backend.
    dst.realize(output);

    double halide = 0, system = 0;
    for (int i = 0; i < 50; i++) {
        double t1 = currentTime();
        dst.realize(output);
        dst.realize(output);
        dst.realize(output);
        double t2 = currentTime();
        memcpy(output.data(), input.data(), input.width());
        memcpy(output.data(), input.data(), input.width());
        memcpy(output.data(), input.data(), input.width());
        double t3 = currentTime();
        system += t3-t2;
        halide += t2-t1;
    }

    printf("system memcpy: %f\n", system);
    printf("halide memcpy: %f\n", halide);

    // memcpy will win by a little bit for large inputs because it uses streaming stores
    if (halide > system * 2) {
        printf("Halide memcpy is slower than it should be.\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
