#include "Halide.h"

using namespace Halide;

const int CHANNELS = 4;

void blur(std::string suffix, ImageParam input) {
    input.set_bounds(2, 0, CHANNELS).set_stride(0, CHANNELS).set_stride(2, 1);

    Var x("x"), y("y"), c("c");

    Func clamped("clamped");
    clamped = BoundaryConditions::repeat_edge(input);

    Func blur_x("blur_x");
    blur_x(x, y, c) = (clamped(x - 1, y, c) +
                       clamped(x, y, c) +
                       clamped(x + 1, y, c)) / 3;

    Func result("result");
    result(x, y, c) = (blur_x(x, y - 1, c) +
                       blur_x(x, y, c) +
                       blur_x(x, y + 1, c)) / 3;

    result.output_buffer().set_bounds(2, 0, CHANNELS).set_stride(0, CHANNELS).set_stride(2, 1);

    Target target = get_target_from_environment();
    result.bound(c, 0, CHANNELS)
          .reorder_storage(c, x, y)
          .reorder(c, x, y);
    if (target.has_gpu_feature()) {
        result.vectorize(c, 4)
              .gpu_tile(x, y, 64, 64);
    } else {
        Var yi("yi");
        result
            .unroll(c)
            .split(y, y, yi, 32)
            .parallel(y)
            .vectorize(x, 4);
        blur_x.store_at(result, y)
            .compute_at(result, yi)
            .reorder(c, x, y)
            .unroll(c)
            .vectorize(x, 4);
    }

    std::string filename("avg_filter");
    result.compile_to_file(filename + suffix, {input});
}

int main(int argc, char** argv) {
    ImageParam input_uint32(UInt(32), 3, "input");
    blur(std::string("_uint32t") + (argc > 1? argv[1]: ""), input_uint32);

    ImageParam input_float(Float(32), 3, "input");
    blur(std::string("_float") + (argc > 1? argv[1]: ""), input_float);
}