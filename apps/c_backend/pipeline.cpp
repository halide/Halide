#include "Halide.h"

using namespace Halide;

// Compile a simple pipeline to an object and to C code.
HalideExtern_2(int, an_extern_func, int, int);

int main(int argc, char **argv) {
    Func f, g, h;
    ImageParam input(UInt(16), 2);
    Var x, y;

    f(x, y) = (input(clamp(x+2, 0, input.width()-1), clamp(y-2, 0, input.height()-1)) * 17)/13;

    h.define_extern("an_extern_stage", {f}, Int(16), 0);

    g(x, y) = f(y, x) + f(x, y) + cast<uint16_t>(an_extern_func(x, y)) + h();

    h.compute_root();
    f.compute_root();
    f.debug_to_file("f.tiff");

    std::vector<Argument> args;
    args.push_back(input);

    g.compile_to_header("pipeline_native.h", args, "pipeline_native");
    g.compile_to_header("pipeline_c.h", args, "pipeline_c");
    g.compile_to_object("pipeline_native.o", args, "pipeline_native", get_host_target());
    g.compile_to_c("pipeline_c.c", args, "pipeline_c", get_host_target());
    return 0;
}
