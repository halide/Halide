#include "Halide.h"

using namespace Halide;

const int CHANNELS = 4;
int main(int argc, char** argv) {
    ImageParam input(Float(32), 2, "input");
    input.set_bounds(1, 0, CHANNELS).set_stride(0, CHANNELS).set_stride(1, 1);

    Func clamped = BoundaryConditions::repeat_edge(input);

    Var x("x"), c("c");
    Func f("f");
    f(x, c) = (clamped(x - 1, c) + clamped(x, c) + clamped(x + 1, c)) / 3.0f;

    // Var fused("");
   	// f.fuse(x, y, fused).gpu_tile(fused, 8).vectorize(Var::gpu_threads(), 4);
    // f.gpu_tile(x, y, 8, 8);

    f.output_buffer().set_bounds(1, 0, CHANNELS).set_stride(0, CHANNELS).set_stride(1, 1);
    f.bound(c, 0, CHANNELS)
     .reorder_storage(c, x)
     .reorder(c, x)
     .gpu_tile(x, 8)
     .vectorize(c);

    Target target = get_target_from_environment();
    std::string filename("avg_filter");
    f.compile_to_file(filename + (argc > 1? argv[1]: ""), {input});
}
