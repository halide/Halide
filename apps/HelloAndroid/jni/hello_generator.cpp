#include "Halide.h"

using namespace Halide;

namespace {

class Hello : public Generator<Hello> {
public:
    Input<Buffer<uint8_t, 2>> input{"input"};
    Output<Buffer<uint8_t, 2>> result{"result"};

    void generate() {
        tone_curve(x) = cast<int16_t>(pow(cast<float>(x) / 256.0f, 1.8f) * 256.0f);

        Func clamped = BoundaryConditions::repeat_edge(input);

        curved(x, y) = tone_curve(clamped(x, y));

        Func sharper;
        sharper(x, y) = 9 * curved(x, y) - 2 * (curved(x - 1, y) + curved(x + 1, y) + curved(x, y - 1) + curved(x, y + 1));

        result(x, y) = cast<uint8_t>(clamp(sharper(x, y), 0, 255));
    }

    void schedule() {
        Var yi;

        tone_curve.compute_root();
        result.split(y, y, yi, 60).vectorize(x, 8).parallel(y);
        curved.store_at(result, y).compute_at(result, yi);

        // We want to handle inputs that may be rotated 180 due to camera module placement.

        // Unset the default stride constraint
        input.dim(0).set_stride(Expr());

        // Make specialized versions for input stride +/-1 to get dense vector loads
        curved.specialize(input.dim(0).stride() == 1);
        curved.specialize(input.dim(0).stride() == -1);
    }

private:
    Var x{"x"}, y{"y"};
    Func tone_curve, curved;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Hello, hello)
