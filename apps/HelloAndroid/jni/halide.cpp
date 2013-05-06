#include <Halide.h>

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

    UniformImage input(UInt(8), 2);

    Var x, y;

    Func tone_curve;
    tone_curve(x) = i16(pow(f32(x)/256.0f, 1.8f) * 256.0f);

    Func curved;
    curved(x, y) = tone_curve(input(x, y));

    Func sharper;
    sharper(x, y) = 9*curved(x, y) - 2*(curved(x-1, y) + curved(x+1, y) + curved(x, y-1) + curved(x, y+1));

    Func result;
    result(x, y) = u8(clamp(sharper(x, y), 0, 255));

    tone_curve.root();
    Var yi;
    curved.chunk(y, yi);
    result.split(y, y, yi, 60).parallel(y).vectorize(x, 8);

    result.compileToFile("halide", "arm.android");    

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
