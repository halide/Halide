#include "Halide.h"

namespace {

Halide::Expr is_interleaved(const Halide::OutputImageParam &p, int channels = 3) {
    return p.stride(0) == channels && p.stride(2) == 1 && p.extent(2) == channels;
}

Halide::Expr is_planar(const Halide::OutputImageParam &p, int channels = 3) {
    return p.stride(0) == 1 && p.extent(2) == channels;
}

class TiledBlurBlur : public Halide::Generator<TiledBlurBlur> {
public:
    ImageParam input{ Int(32), 3, "input" };
    Param<int> width{ "width" };
    Param<int> height{ "height" };

    Func build() {
        // We pass in parameters to tell us where the boundary
        // condition kicks in. This is decoupled from the size of the
        // input tile.

        // In fact, clamping accesses to lie within the input tile
        // using input.min() and input.extent() would tell the calling
        // kernel we can cope with any size input, so it would always
        // pass us 1x1 tiles.

        Var x("x"), y("y"), c("c");

        Func input_clamped = Halide::BoundaryConditions::repeat_edge(input, 0, width, 0, height);

        Func blur("blur");
        blur(x, y, c) = 
            (input_clamped(x - 1, y, c) + input_clamped(x + 1, y, c) +
             input_clamped(x, y - 1, c) + input_clamped(x, y + 1, c)) /
            4.0f;

        // Unset default constraints so that specialization works.
        input.set_stride(0, Expr());
        blur.output_buffer().set_stride(0, Expr());

        // Add specialization for input and output buffers that are both planar.
        blur.specialize(is_planar(input) && is_planar(blur.output_buffer()));

        // Add specialization for input and output buffers that are both interleaved.
        blur.specialize(is_interleaved(input) && is_interleaved(blur.output_buffer()));

        // Note that other combinations (e.g. interleaved -> planar) will work
        // but be relatively unoptimized.

        return blur;
    }
};
Halide::RegisterGenerator<TiledBlurBlur> register_my_gen{"tiled_blur_blur"};

}  // namespace
