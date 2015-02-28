#include "Halide.h"
#include <stdio.h>
#include "my_library/my_library.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    ImageParam in(UInt(8), 2);
    Expr total_width = in.width();

    // Convert to float
    Func as_float;
    as_float(x, y) = cast<float>(in(x, y)) / 255.0f;

    // Brighten
    Func brighten;
    brighten(x, y) = sqrt(as_float(x, y));

    // Do a vignette with a library function
    Expr cx = cast<float>(in.width()/2);
    Expr cy = cast<float>(in.height()/2);
    Expr r = sqrt(cx * cx + cy * cy) / 1.0f;
    Func vignette = my_library::vignette(brighten, cx, cy, r);

    // Do a flip with a library function
    Func flip = my_library::flip(vignette, in.width());

    // The final stage can't be an extern right now if we want to
    // schedule it. Convert back to uint8.
    Func output;
    output(x, y) = cast<uint8_t>(clamp(flip(x, y), 0.0f, 1.0f) * 255.0f + 0.5f);

    Var xo, yo, xi, yi;
    output.tile(x, y, xo, yo, xi, yi, 16, 16);
    output.parallel(yo);

    as_float.compute_at(output, xo);
    brighten.compute_at(output, xo);
    vignette.compute_at(output, xo);
    flip.compute_at(output, xo);

    output.compile_to_file("pipeline", in);

    return 0;
}
