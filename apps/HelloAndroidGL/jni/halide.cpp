#include <Halide.h>

#ifndef M_PI
#define M_PI 3.1415926536
#endif

using namespace Halide;

int main(int argc, char **argv) {
    Param<float> time;

    Var x, y, c;
    Func result;

    Expr kx, ky;
    Expr xx, yy;
    kx = x / 150.0;
    ky = y / 150.0;

    xx = kx + sin(time/3.0);
    yy = ky + sin(time/2.0);

    Expr angle;
    angle = 2 * M_PI * sin(time/20.0);
    kx = kx * cos(angle) - ky * sin(angle);
    ky = kx * sin(angle) + ky * cos(angle);

    Expr v = 0.0;
//    v += sin(kx + time);
    v += sin((ky + time) / 2.0);
    v += sin((kx + ky + time) / 2.0);
    v += sin(sqrt(xx*xx+yy*yy+1.0) + time);

    result(x, y, c) = cast<uint8_t>(
        select(c == 0, 64,
               select(c == 1, cos(M_PI * v),
                      sin(M_PI * v)) * 80 + (255 - 80)));

    result.output_buffer().set_stride(0, 4);
    result.bound(c, 0, 4);
    result.glsl(x, y, c);

    std::vector<Argument> args;
    args.push_back(time);
    result.compile_to_file("halide", args);

    return 0;
}
