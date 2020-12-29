#include "Halide.h"

using namespace Halide;

void blur(std::string suffix, ImageParam input) {
    input.dim(2).set_bounds(0, 4).set_stride(1).dim(0).set_stride(4);

    Var x("x"), y("y"), c("c");

    Func clamped("clamped");
    clamped = BoundaryConditions::repeat_edge(input);

    Func blur_x("blur_x");
    blur_x(x, y, c) = (clamped(x - 1, y, c) +
                       clamped(x, y, c) +
                       clamped(x + 1, y, c)) /
                      3;

    Func result("avg_filter");
    result(x, y, c) = (blur_x(x, y - 1, c) +
                       blur_x(x, y, c) +
                       blur_x(x, y + 1, c)) /
                      3;

    result.output_buffer().dim(2).set_bounds(0, 4).set_stride(1).dim(0).set_stride(4);

    Target target = get_target_from_environment();
    result.bound(c, 0, 4)
        .reorder_storage(c, x, y)
        .reorder(c, x, y);
    if (target.has_gpu_feature() || target.has_feature(Target::OpenGLCompute)) {
        Var xi("xi"), yi("yi");
        result.unroll(c)
            .gpu_tile(x, y, xi, yi, 64, 64);
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

    std::string fn_name = std::string("avg_filter") + suffix;
    result.compile_to_file(fn_name, {input}, fn_name);
}

int main(int argc, char **argv) {
    ImageParam input_uint32(UInt(32), 3, "input");
    blur(std::string("_uint32t") + (argc > 1 ? argv[1] : ""), input_uint32);

    ImageParam input_float(Float(32), 3, "input");
    blur(std::string("_float") + (argc > 1 ? argv[1] : ""), input_float);
}
