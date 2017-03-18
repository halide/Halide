#include "Halide.h"

using namespace Halide;

int main(int argc, char** argv) {
    ImageParam input(UInt(32), 3, "input");
    input.dim(2).set_bounds(0, 4).set_stride(1).dim(0).set_stride(4);

    Var x, y, c;
    Func f("f");
    f(x, y, c) = input(x, y, c) + 1;
    f.bound(c, 0, 4)
     .reorder_storage(c, x, y)
     .reorder(c, x, y);

    f.compute_root();
    f.output_buffer().dim(2).set_bounds(0, 4).set_stride(1).dim(0).set_stride(4);

    Target target = get_target_from_environment();
    if (target.has_gpu_feature() || target.has_feature(Target::OpenGLCompute)) {
        f.unroll(c)
         .gpu_tile(x, y, 64, 64);
    }

    Func g("g");
    g(x, y, c) = f(x, y, c) - 1;
    g.bound(c, 0, 4)
     .reorder_storage(c, x, y)
     .reorder(c, x, y);
    if (target.has_gpu_feature() || target.has_feature(Target::OpenGLCompute)) {
        g.unroll(c)
         .gpu_tile(x, y, 64, 64);
    }
    g.output_buffer().dim(2).set_bounds(0, 4).set_stride(1).dim(0).set_stride(4);

    std::string fn_name = std::string("two_kernels_filter") + (argc > 1 ? argv[1] : "");
    g.compile_to_file(fn_name, {input}, fn_name);
}
