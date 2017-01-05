#include "Halide.h"

using namespace Halide;

// Define a 1D Gaussian blur (a [1 4 6 4 1] filter) of 5 elements.
Expr blur5(Expr x0, Expr x1, Expr x2, Expr x3, Expr x4) {
    // Widen to 16 bits, so we don't overflow while computing the stencil.
    x0 = cast<uint16_t>(x0);
    x1 = cast<uint16_t>(x1);
    x2 = cast<uint16_t>(x2);
    x3 = cast<uint16_t>(x3);
    x4 = cast<uint16_t>(x4);
    return cast<uint8_t>((x0 + 4*x1 + 6*x2 + 4*x3 + x4 + 8)/16);
}

int main(int argc, char **argv) {
    Target target = get_target_from_environment();

    std::cout << "Target: " << target.to_string() << "\n";

    Var x("x"), y("y"), c("c");

    // Takes an 8-bit input image.
    ImageParam input(UInt(8), 3);

    // Apply a boundary condition to the input.
    Func input_bounded("input_bounded");
    input_bounded(x, y, c) = BoundaryConditions::repeat_edge(input)(x, y, c);

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

    // Require the input and output to have 3 channels.
    blur.bound(c, 0, 3);
    input.dim(2).set_bounds(0, 3);

    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        const int vector_size = target.has_feature(Target::HVX_128) ? 128 : 64;

        // The strategy here is to split each scanline of the result
        // into chunks of multiples of the vector size, computing the
        // blur in y at each chunk. We use the RoundUp tail strategy to
        // keep the last chunk's memory accesses aligned.
        Var yo("yo");
        blur.compute_root()
            .hexagon()
            .prefetch(y, 2)
            .split(y, yo, y, 128).parallel(yo)
            .vectorize(x, vector_size * 2, TailStrategy::RoundUp);
        blur_y
            .compute_at(blur, y)
            .vectorize(x, vector_size, TailStrategy::RoundUp);

        // Line buffer the boundary condition, which is expensive. Line
        // buffering it computes it once per row, instead of 5 times per row.
        input_bounded
            .compute_at(blur, y)
            .store_at(blur, yo)
            .align_storage(x, 64)
            .fold_storage(y, 8)
            .vectorize(x, vector_size, TailStrategy::RoundUp);

        // Require scanlines of the input and output to be aligned.
        auto blur_buffer = blur.output_buffer();

        input.dim(0).set_bounds(0, (input.dim(0).extent()/vector_size)*vector_size);
        blur_buffer.dim(0).set_bounds(0, (blur_buffer.dim(0).extent()/vector_size)*vector_size);

        for (int i = 1; i < 3; i++) {
            input.dim(i).set_stride((input.dim(i).stride()/vector_size)*vector_size);
            blur_buffer.dim(i).set_stride((blur_buffer.dim(i).stride()/vector_size)*vector_size);
        }
    } else {
        const int vector_size = target.natural_vector_size<uint8_t>();

        blur.compute_root()
            .parallel(y, 16)
            .vectorize(x, vector_size);
        blur_y.compute_at(blur, y)
            .vectorize(x, vector_size);
    }

    std::stringstream hdr;
    hdr << argv[2] << ".h";
    blur.compile_to_header(hdr.str(), {input}, argv[2], target);
    std::stringstream obj;
    obj << argv[1] << ".o";
    blur.compile_to_object(obj.str(), {input}, argv[2], target);

    return 0;
}
