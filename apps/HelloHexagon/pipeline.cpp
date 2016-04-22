#include "Halide.h"

using namespace Halide;

Expr u16(Expr x) { return cast<uint16_t>(x); }
Expr u8(Expr x) { return cast<uint8_t>(x); }

// Define a 1D Gaussian blur (a [1 4 6 4 1] filter) of 5 elements.
Expr blur5(Expr x0, Expr x1, Expr x2, Expr x3, Expr x4) {
    return u8((u16(x0) + 4*u16(x1) + 6*u16(x2) + 4*u16(x3) + u16(x4) + 8)/16);
}

int main(int argc, char **argv) {
    Target target = get_target_from_environment();

    std::cout << "Target: " << target.to_string() << "\n";

    Var x("x"), y("y"), c("c");

    // Takes an 8-bit input image.
    ImageParam input(UInt(8), 3);

    // Apply a boundary condition to the input.
    Func input_bounded = BoundaryConditions::repeat_edge(input);

    input_bounded.compute_root();

    // Implement this as a separable blur in y followed by x.
    Func blur_y("blur_y");
    blur_y(x, y, c) = blur5(input_bounded(x, y - 2, c),
                            input_bounded(x, y - 1, c),
                            input_bounded(x, y,     c),
                            input_bounded(x, y + 1, c),
                            input_bounded(x, y + 2, c));

    Func blur("blur");
    blur(x, y, c) = blur5(blur_y(x - 2, y, c),
                          blur_y(x - 1, y, c),
                          blur_y(x,     y, c),
                          blur_y(x + 1, y, c),
                          blur_y(x + 2, y, c));

    // Schedule.
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        const int vector_size = target.has_feature(Target::HVX_128) ? 128 : 64;
        blur.compute_root()
            .hexagon()
            .vectorize(x, vector_size);
        blur_y.compute_at(blur, y)
            .align_storage(x, vector_size)
            .vectorize(x, vector_size);
    } else {
        const int vector_size = target.natural_vector_size<uint8_t>();
        blur.compute_root()
            .vectorize(x, vector_size);
        blur_y.compute_at(blur, y)
            .vectorize(x, vector_size);
    }

    blur.compile_to_header("pipeline.h", {input}, "pipeline");
    std::stringstream obj;
    obj << "pipeline-" << argv[1] << ".o";
    blur.compile_to_object(obj.str(), {input}, "pipeline", target);

    return 0;
}
