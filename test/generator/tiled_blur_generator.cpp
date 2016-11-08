#include "Halide.h"

namespace {

Halide::Expr is_interleaved(const Halide::OutputImageParam &p, int channels = 3) {
    return p.stride(0) == channels && p.stride(2) == 1 && p.extent(2) == channels;
}

Halide::Expr is_planar(const Halide::OutputImageParam &p, int channels = 3) {
    return p.stride(0) == 1 && p.extent(2) == channels;
}

class TiledBlur : public Halide::Generator<TiledBlur> {
public:
    ImageParam input{ Int(32), 3, "input" };

    Func build() {
        // This is the outermost pipeline, so input width and height
        // are meaningful. If you want to be able to call this outer
        // pipeline in a tiled fashion itself, then you should pass in
        // width and height as params, as with the blur above.

        Var x("x"), y("y"), c("c");

        Func brighter1("brighter1");
        brighter1(x, y, c) = input(x, y, c) * 1.2f;

        Func tiled_blur;
        tiled_blur.define_extern(
            "tiled_blur_blur",
            { brighter1, input.width(), input.height() },
            Float(32), 3);

        Func brighter2("brighter2");
        brighter2(x, y, c) = tiled_blur(x, y, c) * 1.2f;

        Var xi, yi;
        brighter2.reorder(c, x, y).tile(x, y, xi, yi, 32, 32);
        tiled_blur.compute_at(brighter2, x);
        brighter1.compute_at(brighter2, x);

        // Let's see what tiled_blur decides that it needs from
        // brighter1. They should be 34x34 tiles, but clamped to fit
        // within the input, so they'll often be 33x34, 34x33, or
        // 33x33 near the boundaries
        brighter1.trace_realizations();

        // Unset default constraints so that specialization works.
        input.set_stride(0, Expr());
        brighter2.output_buffer().set_stride(0, Expr());

        // Add specialization for input and output buffers that are both planar.
        brighter2.specialize(is_planar(input) && is_planar(brighter2.output_buffer()));

        // Add specialization for input and output buffers that are both interleaved.
        brighter2.specialize(is_interleaved(input) && is_interleaved(brighter2.output_buffer()));

        // Note that other combinations (e.g. interleaved -> planar) will work
        // but be relatively unoptimized.

        return brighter2;
    }
};
Halide::RegisterGenerator<TiledBlur> register_my_gen{"tiled_blur"};

}  // namespace
