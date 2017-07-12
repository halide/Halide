#include "Halide.h"

namespace {

class HalideBlurGLSL : public Halide::Generator<HalideBlurGLSL> {
public:
    ImageParam input8{UInt(8), 3, "input8"};
    Func build() {
        assert(get_target().has_feature(Target::OpenGL));

        Func blur_x("blur_x"), blur_y("blur_y"), out("blur_filter");
        Var x("x"), y("y"), c("c");

        // The algorithm
        Func input;
        input(x,y,c) = cast<float>(input8(clamp(x, input8.left(), input8.right()),
                                          clamp(y, input8.top(), input8.bottom()), c)) / 255.f;
        blur_x(x, y, c) = (input(x, y, c) + input(x+1, y, c) + input(x+2, y, c)) / 3;
        blur_y(x, y, c) = (blur_x(x, y, c) + blur_x(x, y+1, c) + blur_x(x, y+2, c)) / 3;
        out(x, y, c) = cast<uint8_t>(blur_y(x, y, c) * 255.f);

        // Schedule for GLSL
        input8.dim(2).set_bounds(0, 3);
        out.bound(c, 0, 3);
        out.glsl(x, y, c);

        return out;
    }
};

Halide::RegisterGenerator<HalideBlurGLSL> register_me{"halide_blur_glsl"};

}  // namespace
