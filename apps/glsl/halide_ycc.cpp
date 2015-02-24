#include "Halide.h"

using namespace Halide;

void RgbToYcc() {
    ImageParam input8(UInt(8), 3);
    Func out("out");
    Var x("x"), y("y"), c("c");

    // The algorithm
    Func input("input");
    input(x, y, c) = cast<float>(input8(x, y, c)) / 255.0f;

    Func Y("Y"), Cb("Cb"), Cr("Cr");
    Y(x,y) = 16.f/255.f + (0.257f * input(x, y, 0) +
                           0.504f * input(x, y, 1) +
                           0.098f * input(x, y, 2));
    Cb(x,y) = 128.f/255.f + (0.439f * input(x, y, 0) +
                             -0.368f * input(x, y, 1) +
                             -0.071f * input(x, y, 2));
    Cr(x,y) = 128.f/255.f + (-0.148f * input(x, y, 0) +
                             -0.291f * input(x, y, 1) +
                             0.439f * input(x, y, 2));
    out(x, y, c) = cast<uint8_t>(select(c==0, Y(x,y),
                                        select(c==1, Cb(x,y),
                                               select(c==2, Cr(x,y),
                                                      0.0f))) * 255.f);

    // Schedule for GLSL
    input8.set_bounds(2, 0, 3);
    out.bound(c, 0, 3);
    out.glsl(x, y, c);
    out.compute_root();

    Func cpuout("ycc_filter");
    cpuout(x,y,c) = out(x,y,c);

    std::vector<Argument> args;
    args.push_back(input8);
    cpuout.compile_to_object("ycc.o", args);
    cpuout.compile_to_header("ycc.h", args);
}

int main() {
    RgbToYcc();
    return 0;
}
