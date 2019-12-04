#include "Halide.h"

namespace {

class RgbToYcc : public Halide::Generator<RgbToYcc> {
public:
    Input<Buffer<uint8_t>> input8{"input8", 3};
    Output<Buffer<uint8_t>> out{"out", 3};
    void generate() {
        assert(get_target().has_feature(Target::OpenGL));
        Var x("x"), y("y"), c("c");

        // The algorithm
        Func input("input");
        input(x, y, c) = cast<float>(input8(x, y, c)) / 255.0f;

        Func Y("Y"), Cb("Cb"), Cr("Cr");
        Y(x, y) = 16.f / 255.f + (0.257f * input(x, y, 0) +
                                  0.504f * input(x, y, 1) +
                                  0.098f * input(x, y, 2));
        Cb(x, y) = 128.f / 255.f + (0.439f * input(x, y, 0) +
                                    -0.368f * input(x, y, 1) +
                                    -0.071f * input(x, y, 2));
        Cr(x, y) = 128.f / 255.f + (-0.148f * input(x, y, 0) +
                                    -0.291f * input(x, y, 1) +
                                    0.439f * input(x, y, 2));
        out(x, y, c) = cast<uint8_t>(select(c == 0, Y(x, y),
                                            c == 1, Cb(x, y),
                                            c == 2, Cr(x, y),
                                            0.0f) *
                                     255.f);

        // Schedule for GLSL
        input8.dim(2).set_bounds(0, 3);
        out.bound(c, 0, 3);
        out.glsl(x, y, c);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(RgbToYcc, halide_ycc_glsl)
