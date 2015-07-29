#include "Halide.h"

using namespace Halide;

const int CHANNELS = 4;
int main(int argc, char** argv) {
    ImageParam input(UInt(32), 3, "input");
    input.set_bounds(2, 0, CHANNELS).set_stride(0, CHANNELS).set_stride(2, 1);

    Var x, y, c;
    Func f("f");
    f(x, y, c) = input(x, y, c) + 1;
    f.bound(c, 0, CHANNELS)
     .reorder_storage(c, x, y)
     .reorder(c, x, y);

    f.compute_root();
    f.output_buffer().set_bounds(2, 0, CHANNELS).set_stride(0, CHANNELS).set_stride(2, 1);

    Target target = get_target_from_environment();
    if (target.has_gpu_feature()) {
        f.vectorize(c, 4)
         .gpu_tile(x, y, 64, 64);
    }

    Func g("g");
    g(x, y, c) = f(x, y, c) - 1;
    g.bound(c, 0, CHANNELS)
     .reorder_storage(c, x, y)
     .reorder(c, x, y);
    if (target.has_gpu_feature()) {
        g.vectorize(c, 4)
         .gpu_tile(x, y, 64, 64);
    }
    g.output_buffer().set_bounds(2, 0, CHANNELS).set_stride(0, CHANNELS).set_stride(2, 1);

    std::string filename("two_kernels_filter");
    g.compile_to_file(filename + (argc > 1? argv[1]: ""), {input});
}
