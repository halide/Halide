#include "Halide.h"

namespace {

template<typename T>
Halide::Expr is_interleaved(const T &p, int channels = 3) {
    return p.dim(0).stride() == channels && p.dim(2).stride() == 1 && p.dim(2).extent() == channels;
}

template<typename T>
Halide::Expr is_planar(const T &p, int channels = 3) {
    return p.dim(0).stride() == 1 && p.dim(2).extent() == channels;
}

}  // namespace

#if HALIDE_PREFER_G2_GENERATORS

namespace {

using namespace Halide;

Var x("x"), y("y"), c("c");

// A general utility that deals only with Funcs and has no schedule.
Func blur2x2(Func input, Expr width, Expr height) {
    Func input_clamped =
        Halide::BoundaryConditions::repeat_edge(input, {{0, width}, {0, height}});

    Func blur("blur2x2");
    blur(x, y, c) =
        (input_clamped(x - 1, y, c) + input_clamped(x + 1, y, c) +
         input_clamped(x, y - 1, c) + input_clamped(x, y + 1, c)) /
        4.0f;

    return blur;
}

// A wrapper for the general version that deals with Image inputs and outputs and has a real schedule.
Func blur2x2_scheduled(const Target &target, ImageParam input, Expr width, Expr height) {
    Func blur = blur2x2(input, width, height);

    // Unset default constraints so that specialization works.
    input.dim(0).set_stride(Expr());
    blur.output_buffer().dim(0).set_stride(Expr());

    // Add specialization for input and output buffers that are both planar.
    blur.specialize(is_planar(input) && is_planar(blur.output_buffer()))
        .vectorize(x, target.natural_vector_size<float>());

    // Add specialization for input and output buffers that are both interleaved.
    blur.specialize(is_interleaved(input) && is_interleaved(blur.output_buffer()))
        .reorder(c, x, y)
        .vectorize(c);

    return blur;
}

}  // namespace

HALIDE_REGISTER_G2(
    blur2x2_scheduled,  // actual C++ fn
    blur2x2,            // build-system name
    Target(),
    Input("input", Float(32), 3),
    Input("width", Int(32)),
    Input("height", Int(32)),
    Output("output", Float(32), 3))

#else  // not HALIDE_PREFER_G2_GENERATORS

namespace {

// A trivial 2x2 blur.
class Blur2x2 : public Halide::Generator<Blur2x2> {
public:
    Input<Buffer<float, 3>> input{"input"};
    Input<int32_t> width{"width"};
    Input<int32_t> height{"height"};
    Output<Buffer<float, 3>> blur{"blur"};

    void generate() {
        // We pass in parameters to tell us where the boundary
        // condition kicks in; this allows us to decouple from the size of the
        // input tile (if any).

        // (In fact, if we are being used as an extern stage for tiled processing,
        // clamping accesses to lie within the input tile using input.min() and
        // input.extent() would tell the calling kernel we can cope with any size
        // input, so it would always pass us 1x1 tiles.)

        Func input_clamped =
            Halide::BoundaryConditions::repeat_edge(input, {{0, width}, {0, height}});

        blur(x, y, c) =
            (input_clamped(x - 1, y, c) + input_clamped(x + 1, y, c) +
             input_clamped(x, y - 1, c) + input_clamped(x, y + 1, c)) /
            4.0f;
    }

    void schedule() {
        // Unset default constraints so that specialization works.
        input.dim(0).set_stride(Expr());
        blur.dim(0).set_stride(Expr());

        // Add specialization for input and output buffers that are both planar.
        blur.specialize(is_planar(input) && is_planar(blur))
            .vectorize(x, natural_vector_size<float>());

        // Add specialization for input and output buffers that are both interleaved.
        blur.specialize(is_interleaved(input) && is_interleaved(blur))
            .reorder(c, x, y)
            .vectorize(c);

        // Note that other combinations (e.g. interleaved -> planar) will work
        // but be relatively unoptimized.
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Blur2x2, blur2x2)

#endif  // HALIDE_PREFER_G2_GENERATORS
