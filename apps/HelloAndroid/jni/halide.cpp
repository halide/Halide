#include <Halide.h>

using namespace Halide;

Expr u8(Expr x) {
    return cast(UInt(8), x);
}

Expr i16(Expr x) {
    return cast(Int(16), x);
}

int main(int argc, char **argv) {

    UniformImage input(UInt(8), 2);

    Var x, y;

    Func clamped;
    clamped(x, y) = i16(input(clamp(x, 0, input.width()-1), clamp(y, 0, input.height()-1)));

    Func sharper;
    sharper(x, y) = 9*clamped(x, y) - 2*(clamped(x-1, y) + clamped(x+1, y) + clamped(x, y-1) + clamped(x, y+1));

    Func result;
    result(x, y) = u8(clamp(sharper(x, y), 0, 255));

    clamped.root().parallel(y);
    result.vectorize(x, 8).parallel(y);

    result.compileToFile("halide", "arm.android");    

    return 0;
}










   /* 13 ms
      clamped.root();
    */        

    /* 14 ms
    Var yo, yi;
    result.split(y, yo, yi, 8);
    clamped.chunk(yo);
    */

    /* 12 ms
    clamped.root();
    result.vectorize(x, 8);
    */

    /* 6 ms
    clamped.root().parallel(y);
    result.vectorize(x, 8).parallel(y);
    */

    /* 8 ms
    clamped.root().parallel(y);
    result.vectorize(x, 8).parallel(y);
    sharper.root().vectorize(x, 8).parallel(y);
    */

    /* 7 ms
    Var yo, yi;
    clamped.chunk(yo);
    result.split(y, yo, yi, 36).vectorize(x, 8).parallel(yo);
    */
