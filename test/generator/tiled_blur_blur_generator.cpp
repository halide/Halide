#include "Halide.h"

namespace {

class TiledBlurBlur : public Halide::Generator<TiledBlurBlur> {
public:
    GeneratorParam<bool> is_interleaved{ "is_interleaved", false };
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

        Func blur("blur");
        blur(x, y, c) =
            (input(clamp(x - 1, 0, width - 1), y, c) + input(clamp(x + 1, 0, width - 1), y, c) +
             input(x, clamp(y - 1, 0, height - 1), c) + input(x, clamp(y + 1, 0, height - 1), c)) /
            4.0f;

        if (is_interleaved) {
            input.set_stride(2, 1).set_stride(0, 3).set_bounds(2, 0, 3);
            blur.output_buffer().set_stride(2, 1).set_stride(0, 3).set_bounds(2, 0, 3);
        }
        return blur;
    }
};
Halide::RegisterGenerator<TiledBlurBlur> register_my_gen{"tiled_blur_blur"};

}  // namespace
