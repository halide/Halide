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

    f.output_buffer().set_bounds(1, 0, CHANNELS).set_stride(0, CHANNELS).set_stride(1, 1);

    Target target = get_target_from_environment();
    f.bound(c, 0, CHANNELS)
     .reorder_storage(c, x)
     .reorder(c, x)
     .vectorize(c);
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, 1024);
    } else {
        // f.parallel(x, 64);
    }

    std::string filename("avg_filter");
    f.compile_to_file(filename + (argc > 1? argv[1]: ""), {input});
}
