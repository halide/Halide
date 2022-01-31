#include "Halide.h"

namespace {

using Halide::saturating_cast;

template<typename T>
Halide::Expr is_interleaved(const T &p, int channels = 3) {
    return p.dim(0).stride() == channels && p.dim(2).stride() == 1 && p.dim(2).extent() == channels;
}

template<typename T>
Halide::Expr is_planar(const T &p, int channels = 3) {
    return p.dim(0).stride() == 1 && p.dim(2).extent() == channels;
}

class TiledBlur : public Halide::Generator<TiledBlur> {
public:
    Input<Buffer<uint8_t, 3>> input{"input"};
    Output<Buffer<uint8_t, 3>> output{"output"};

    void generate() {
        Expr input_float = cast<float>(input(x, y, c)) / 255.f;

        // This is the outermost pipeline, so input width and height
        // are meaningful. If you want to be able to call this outer
        // pipeline in a tiled fashion itself, then you should pass in
        // width and height as params, as with the blur above.
        brightened(x, y, c) = input_float * 1.2f;

        tiled_blur.define_extern(
            "blur2x2",
            {brightened, input.dim(0).extent(), input.dim(1).extent()},
            Float(32), 3);

        Expr tiled_blur_brightened = tiled_blur(x, y, c) * 1.2f;

        output(x, y, c) = saturating_cast<uint8_t>(tiled_blur_brightened * 255.f);
    }

    void schedule() {
        Var xi, yi;
        output.reorder(c, x, y).tile(x, y, xi, yi, 32, 32);
        tiled_blur.compute_at(output, x);
        brightened.compute_at(output, x);

        // Let's see what tiled_blur decides that it needs from
        // brightened. They should be 34x34 tiles, but clamped to fit
        // within the input, so they'll often be 33x34, 34x33, or
        // 33x33 near the boundaries
        brightened.trace_realizations();

        // Unset default constraints so that specialization works.
        input.dim(0).set_stride(Expr());
        output.dim(0).set_stride(Expr());

        // Add specialization for input and output buffers that are both planar.
        output.specialize(is_planar(input) && is_planar(output))
            .vectorize(xi, natural_vector_size<float>());

        // Add specialization for input and output buffers that are both interleaved.
        output.specialize(is_interleaved(input) && is_interleaved(output));

        // Note that other combinations (e.g. interleaved -> planar) will work
        // but be relatively unoptimized.
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
    Func tiled_blur{"tiled_blur"};
    Func brightened{"brightened"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(TiledBlur, tiled_blur)
