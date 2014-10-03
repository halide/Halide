#include "Halide.h"

using namespace Halide;

namespace {

class TiledBlur : public Generator<TiledBlur> {
public:
    GeneratorParam<bool> is_interleaved{"is_interleaved", false};
    ImageParam input{Int(32), 3, "input"};

    Func build() override {
        // This is the outermost pipeline, so input width and height
        // are meaningful. If you want to be able to call this outer
        // pipeline in a tiled fashion itself, then you should pass in
        // width and height as params, as with the blur above.

        Var x("x"), y("y"), c("c");

        Func brighter1("brighter1");
        brighter1(x, y, c) = input(x, y, c) * 1.2f;

        Func tiled_blur("tiled_blur");
        std::vector<ExternFuncArgument> args(3);
        args[0] = brighter1;
        // Pass input.width() and input.height() down to the blur so
        // it knows the global boundary it should clamp to. We assume
        // the global min is at 0, 0.
        args[1] = input.width();
        args[2] = input.height();
        tiled_blur.define_extern(
            is_interleaved ? "tiled_blur_blur_interleaved" : "tiled_blur_blur",
            args, Float(32), 3);

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

        if (is_interleaved) {
            brighter1.reorder_storage(c, x, y);
            tiled_blur.reorder_storage(tiled_blur.args()[2], tiled_blur.args()[0], tiled_blur.args()[1]);
            input.set_stride(2, 1).set_stride(0, 3).set_bounds(2, 0, 3);
            brighter2.output_buffer().set_stride(2, 1).set_stride(0, 3).set_bounds(2, 0, 3);
        }

        return brighter2;
    }
};

RegisterGenerator<TiledBlur> register_my_gen("tiled_blur");

}  // namespace
