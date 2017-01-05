#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<float> input(1024, 1024);

    Func upsampled("upsampled");
    Func upsampledx("upsampledx");
    Var x("x"), y("y");

    Func clamped("clamped");
    clamped(x, y) = input(x, y);

    upsampledx(x, y) = select((x % 2) == 0,
                              clamped(x, y),
                              clamped(x+1, y));
    upsampled(x, y) = upsampledx(x, y);

    Var xi("xi"), yi("yi");
    clamped.compute_root(); // passes if this is removed, switched to inline
    upsampled
        .split(y, y, yi, 8)
        .reorder(yi, y, x)
        .compute_root();

    upsampledx.compute_at(upsampled, yi);

    upsampled.realize(100, 100);

    return 0;
}
