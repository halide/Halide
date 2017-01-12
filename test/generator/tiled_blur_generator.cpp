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

class TiledBlur : public Halide::Generator<TiledBlur> {
public:
    Input<Buffer<int32_t>> input{ "input", 3 };
    Output<Buffer<float>> brighter2{ "brighter2", 3 };

    void generate() {
        // This is the outermost pipeline, so input width and height
        // are meaningful. If you want to be able to call this outer
        // pipeline in a tiled fashion itself, then you should pass in
        // width and height as params, as with the blur above.
        brighter1(x, y, c) = input(x, y, c) * 1.2f;

        tiled_blur.define_extern(
            "tiled_blur_blur",
            { brighter1, input.dim(0).extent(), input.dim(1).extent() },
            Float(32), 3);

        brighter2(x, y, c) = tiled_blur(x, y, c) * 1.2f;
    }

    void schedule() {
        Var xi, yi;
        Func(brighter2).reorder(c, x, y).tile(x, y, xi, yi, 32, 32);
        tiled_blur.compute_at(brighter2, x);
        brighter1.compute_at(brighter2, x);

        // Let's see what tiled_blur decides that it needs from
        // brighter1. They should be 34x34 tiles, but clamped to fit
        // within the input, so they'll often be 33x34, 34x33, or
        // 33x33 near the boundaries
        brighter1.trace_realizations();

        // Unset default constraints so that specialization works.
        input.dim(0).set_stride(Expr());
        brighter2.dim(0).set_stride(Expr());

        // Add specialization for input and output buffers that are both planar.
        Func(brighter2).specialize(is_planar(input) && is_planar(brighter2));

        // Add specialization for input and output buffers that are both interleaved.
        Func(brighter2).specialize(is_interleaved(input) && is_interleaved(brighter2));

        // Note that other combinations (e.g. interleaved -> planar) will work
        // but be relatively unoptimized.
    }
private:
    Var x{"x"}, y{"y"}, c{"c"};
    Func tiled_blur{"tiled_blur"};
    Func brighter1{"brighter1"};
};

HALIDE_REGISTER_GENERATOR(TiledBlur, "tiled_blur")

}  // namespace
