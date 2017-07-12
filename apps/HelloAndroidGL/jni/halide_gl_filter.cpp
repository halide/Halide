#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Param<float> time;

    const float pi = 3.1415926536;

    Var x, y, c;
    Func result;

    Expr kx, ky;
    Expr xx, yy;
    kx = x / 150.0f;
    ky = y / 150.0f;

    xx = kx + sin(time/3.0f);
    yy = ky + sin(time/2.0f);

    Expr angle;
    angle = 2 * pi * sin(time/20.0f);
    kx = kx * cos(angle) - ky * sin(angle);
    ky = kx * sin(angle) + ky * cos(angle);

    Expr v = 0.0f;
    v += sin((ky + time) / 2.0f);
    v += sin((kx + ky + time) / 2.0f);
    v += sin(sqrt(xx * xx + yy * yy + 1.0f) + time);

    result(x, y, c) = cast<uint8_t>(
        select(c == 0, 32,
               select(c == 1, cos(pi * v),
                      sin(pi * v)) * 80 + (255 - 80)));

    result.output_buffer().set_stride(0, 4);
    result.bound(c, 0, 4);
    result.glsl(x, y, c);

    result.compile_to_file("halide_gl_filter", {time}, "halide_gl_filter");

    return 0;
}
