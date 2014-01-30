#include <Halide.h>

using namespace Halide;

void Blur() {
    ImageParam input8(UInt(8), 3);
    Func blur_x("blur_x"), out("blur_filter");
    Var x("x"), y("y"), c("c");

    // The algorithm
    Func input;
    input(x,y,c) = cast(Float(32), input8(x,y,c));
    blur_x(x, y, c) = (input(x, y, c) + input(x+1, y, c) + input(x+2, y, c))/3;
    out(x, y, c) = cast(UInt(8),
                        (blur_x(x, y, c) + blur_x(x, y+1, c) + blur_x(x, y+2, c))/3);

    // Schedule for GLSL
    out.reorder(c, x, y);
    out.glsl(x, y, c);
    out.bound(c, 0, 4);
    out.vectorize(c);
//    out.unroll(c);

    std::vector<Argument> args;
    args.push_back(input8);
    out.compile_to_object("blur.o", args);
    out.compile_to_header("blur.h", args);
}


int main(int argc, char **argv) {
    Blur();
    return 0;
}
