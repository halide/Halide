#include "Halide.h"

using namespace Halide;


Expr u8(Expr x) {
    return cast(UInt(8), x);
}

Expr i16(Expr x) {
    return cast(Int(16), x);
}

Expr f32(Expr x) {
    return cast(Float(32), x);
}

int main(int argc, char **argv) {

    ImageParam input(UInt(8), 2, "input");

    Var x, y;

    Func tone_curve;
    tone_curve(x) = i16(pow(f32(x)/256.0f, 1.8f) * 256.0f);

    Func clamped = BoundaryConditions::repeat_edge(input);

    Func curved;
    curved(x, y) = tone_curve(clamped(x, y));

    Func sharper;
    sharper(x, y) = 9*curved(x, y) - 2*(curved(x-1, y) + curved(x+1, y) + curved(x, y-1) + curved(x, y+1));

    Func result("result");
    result(x, y) = u8(clamp(sharper(x, y), 0, 255));

    tone_curve.compute_root();
    Var yi;

    result.split(y, y, yi, 60).vectorize(x, 8).parallel(y);
    curved.store_at(result, y).compute_at(result, yi);

    /*
      curved.compute_root().vectorize(x, 8).gpu_tile(x, y, 2, 16, Device_OpenCL);
      result.compute_root().vectorize(x, 8).gpu_tile(x, y, 2, 16, Device_OpenCL);
    */

    // We want to handle inputs that may be rotated 180 due to camera module placement.

    // Unset the default stride constraint
    input.set_stride(0, Expr());

    // Make specialized versions for input stride +/-1 to get dense vector loads
    curved.specialize(input.stride(0) == 1);
    curved.specialize(input.stride(0) == -1);
        
    std::vector<Argument> args;
    args.push_back(input);
    result.compile_to_file("halide_generated", args);

    return 0;
}







// All inline
// Tone curve root
// Result vectorized
// Curved root
// Result and curved parallelized
// result split into tiles of height 60, curve chunked (y, yi)

/*
Var yi;
tone_curve.root();
curved.chunk(y, yi);
result.root().vectorize(x, 8).split(y, y, yi, 60).parallel(y);
*/
