#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input8(UInt(8), 3);
    Func blur_x("blur_x"), blur_y("blur_y"), out("blur_filter");
    Var x("x"), y("y"), c("c");

    // The algorithm
    Func input;
    input(x,y,c) = cast<float>(input8(clamp(x, input8.left(), input8.right()),
                                      clamp(y, input8.top(), input8.bottom()), c)) / 255.f;
    blur_x(x, y, c) = (input(x, y, c) + input(x+1, y, c) + input(x+2, y, c)) / 3;
    blur_y(x, y, c) = (blur_x(x, y, c) + blur_x(x, y+1, c) + blur_x(x, y+2, c)) / 3;
    out(x, y, c) = cast<uint8_t>(blur_y(x, y, c) * 255.f);

    // Schedule for GLSL
    input8.set_bounds(2, 0, 3);
    out.bound(c, 0, 3);
    out.glsl(x, y, c);

    std::vector<Argument> args;
    args.push_back(input8);
    out.compile_to_object("blur.o", args);
    out.compile_to_header("blur.h", args);
    return 0;
}
