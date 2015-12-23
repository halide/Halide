#include "Halide.h"

namespace {

class TiledBlurBlur : public Halide::Generator<TiledBlurBlur> {
public:
    GeneratorParam<bool> is_interleaved{ "is_interleaved", false };
    ImageParam input{ Float(32), 3, "input" };
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

        Func blur("blur");
        blur(x, y, c) =
            (input(clamp(x - 1, 0, width - 1), y, c) + input(clamp(x + 1, 0, width - 1), y, c) +
             input(x, clamp(y - 1, 0, height - 1), c) + input(x, clamp(y + 1, 0, height - 1), c)) /
            4.0f;

        if (is_interleaved) {
            input.dim(0).set_stride(3);
            input.dim(2).set_stride(1).set_bounds(0, 3);
            blur.output_buffer().dim(0).set_stride(3);
            blur.output_buffer().dim(2).set_stride(1).set_bounds(0, 3);
        }
        return blur;
    }
};
Halide::RegisterGenerator<TiledBlurBlur> register_my_gen{"tiled_blur_blur"};

}  // namespace
