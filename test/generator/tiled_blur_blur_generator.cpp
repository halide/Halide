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

class TiledBlurBlur : public Halide::Generator<TiledBlurBlur> {
public:
    Input<Buffer<int32_t, 3>> input{ "input" };
    Input<int32_t> width{ "width" };
    Input<int32_t> height{ "height" };

    Output<Buffer<float, 3>> blur{ "blur" };

    void generate() {
        // We pass in parameters to tell us where the boundary
        // condition kicks in. This is decoupled from the size of the
        // input tile.

        // In fact, clamping accesses to lie within the input tile
        // using input.min() and input.extent() would tell the calling
        // kernel we can cope with any size input, so it would always
        // pass us 1x1 tiles.

        Func input_clamped = Halide::BoundaryConditions::repeat_edge(
            input, 0, width, 0, height);

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
        Func(blur).specialize(is_planar(input) && is_planar(blur));

        // Add specialization for input and output buffers that are both interleaved.
        Func(blur).specialize(is_interleaved(input) && is_interleaved(blur));

        // Note that other combinations (e.g. interleaved -> planar) will work
        // but be relatively unoptimized.

    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
};

HALIDE_REGISTER_GENERATOR(TiledBlurBlur, "tiled_blur_blur")

}  // namespace
