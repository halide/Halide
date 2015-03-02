#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    ImageParam in(Float(32), 2);
    Param<float> center_x, center_y, radius;

    Expr dx = x - center_x, dy = y - center_y;
    Expr r = sqrt(dx * dx + dy * dy);
    Expr scale = max(0.0f, 1.0f - r / radius);

    Func vignette;
    vignette(x, y) = in(x, y) * scale;

    // Any scheduling for a goes here
    vignette.vectorize(x, 4);

    vignette.compile_to_file("vignette_impl", in, center_x, center_y, radius);

    return 0;
}

