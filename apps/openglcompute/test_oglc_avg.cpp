#include "Halide.h"

using namespace Halide;

const int CHANNELS = 4;
int main(int argc, char** argv) {
    ImageParam input(Float(32), 2, "input");

    Func clamped = BoundaryConditions::repeat_edge(input);

    Var x, y;
    Func f("f");
    f(x, y) = (clamped(x - 1, y) + clamped(x, y) + clamped(x, y + 1)) / 3.0f;

    Var fused;
   	f.fuse(x, y, fused).gpu_tile(fused, 8).vectorize(Var::gpu_blocks(), 4);
    // f.gpu_tile(x, y, 8, 8);

    Target target = get_target_from_environment();
    std::string filename("avg_filter");
    f.compile_to_file(filename + (argc > 1? argv[1]: ""), {input});
}
