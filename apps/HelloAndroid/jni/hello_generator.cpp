#include "Halide.h"

using namespace Halide;

namespace {

class Hello : public Generator<Hello> {
public:
    ImageParam input{UInt(8), 2, "input"};

    Func build() {
        Var x, y;

        Func tone_curve;
        tone_curve(x) = cast<int16_t>(pow(cast<float>(x)/256.0f, 1.8f) * 256.0f);
        tone_curve.compute_root();

        Func clamped = BoundaryConditions::repeat_edge(input);

        Func curved;
        curved(x, y) = tone_curve(clamped(x, y));

        Func sharper;
        sharper(x, y) = 9*curved(x, y) - 2*(curved(x-1, y) + curved(x+1, y) + curved(x, y-1) + curved(x, y+1));

        Func result;
        result(x, y) = cast<uint8_t>(clamp(sharper(x, y), 0, 255));


        Var yi;

        result.split(y, y, yi, 60).vectorize(x, 8).parallel(y);
        curved.store_at(result, y).compute_at(result, yi);

        // We want to handle inputs that may be rotated 180 due to camera module placement.

        // Unset the default stride constraint
        input.set_stride(0, Expr());

        // Make specialized versions for input stride +/-1 to get dense vector loads
        curved.specialize(input.stride(0) == 1);
        curved.specialize(input.stride(0) == -1);

        return result;
    }

};

HALIDE_REGISTER_GENERATOR(Hello, "hello")

}  // namespace
