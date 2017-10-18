#include "Halide.h"

namespace {

class sRGBToLinear : public Halide::Generator<sRGBToLinear> {
public:
    Input<Func>  srgb{"srgb"};
    Output<Func> linear{"linear"};

    void generate() {
        using Halide::_;
        Var x("x"), y("y"), yi("yi");

        linear(x, y, _) = select(srgb(x, y, _) <= 0.04045f,
                                 srgb(x, y, _) / 12.92f,
                                 pow(((srgb(x, y, _) + .055f) / (1.0f + .055f)), 2.4f));

        if (auto_schedule) {
            const int W = 1536, H = 2560, C = 4;
            // Wart: Input<Func> are defined with Vars we don't know.
            // Might be x,y but might be _0,_1. Use the args() to work around.
            srgb.estimate(srgb.args()[0], 0, W)
                .estimate(srgb.args()[1], 0, H);
            for (int i = 2; i < srgb.args().size(); ++i) {
                srgb.estimate(srgb.args()[i], 0, C);
            }
            linear.estimate(x, 0, W)
                  .estimate(y, 0, H);
            for (int i = 2; i < linear.args().size(); ++i) {
                linear.estimate(linear.args()[i], 0, C);
            }
        } else {
            linear.split(y, y, yi, 8).parallel(y).vectorize(x, 8);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(sRGBToLinear, srgb_to_linear)
