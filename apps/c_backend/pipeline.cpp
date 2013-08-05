#include <Halide.h>

using namespace Halide;

// Compile a simple pipeline to an object and to C code.

int main(int argc, char **argv) {
    Func f, g;
    ImageParam input(UInt(16), 2);
    Var x, y;

    f(x, y) = (input(clamp(x+2, 0, input.width()-1), clamp(y-2, 0, input.height()-1)) * 17)/13;
    g(x, y) = f(y, x) + f(x, y);

    std::vector<Argument> args;
    args.push_back(input);

    g.compile_to_header("pipeline_native.h", args, "pipeline_native");
    g.compile_to_header("pipeline_c.h", args, "pipeline_c");
    g.compile_to_object("pipeline_native.o", args, "pipeline_native");
    g.compile_to_c("pipeline_c.c", args, "pipeline_c");
    return 0;
}
