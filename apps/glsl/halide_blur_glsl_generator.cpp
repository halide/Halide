#include "Halide.h"

namespace {

class HalideBlurGLSL : public Halide::Generator<HalideBlurGLSL> {
public:
    Input<Buffer<uint8_t>> input8{"input8", 3};
    Output<Buffer<uint8_t>> blur_filter{"blur_filter", 3};
    void generate() {
        assert(get_target().has_feature(Target::OpenGL));

        Func blur_x("blur_x"), blur_y("blur_y");
        Var x("x"), y("y"), c("c");

        // The algorithm
        Func input;
        input(x, y, c) = cast<float>(input8(clamp(x, input8.dim(0).min(), input8.dim(0).max()),
                                            clamp(y, input8.dim(1).min(), input8.dim(1).max()), c)) /
                         255.f;
        blur_x(x, y, c) = (input(x, y, c) + input(x + 1, y, c) + input(x + 2, y, c)) / 3;
        blur_y(x, y, c) = (blur_x(x, y, c) + blur_x(x, y + 1, c) + blur_x(x, y + 2, c)) / 3;
        blur_filter(x, y, c) = cast<uint8_t>(blur_y(x, y, c) * 255.f);

        // Schedule for GLSL
        input8.dim(2).set_bounds(0, 3);
        blur_filter.bound(c, 0, 3);
        blur_filter.glsl(x, y, c);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(HalideBlurGLSL, halide_blur_glsl)
