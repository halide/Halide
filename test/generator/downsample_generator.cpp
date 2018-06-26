#include "Halide.h"

namespace {

// Downsamples an image in tiles. This generator exists primarily to verify
// that bounds inference properly handles variable sized shifts. Without
// support for variable size shifts, input will be accessed in an unbounded way.
class Downsample : public Halide::Generator<Downsample> {
public:
    Input<int32_t> log_scale{ "log_scale" };
    Input<Func> input{ "input", UInt(8), 2};

    Output<Func> output{ "output", UInt(8), 2 };

    void generate() {
        Expr scale = 1 << log_scale;
        Expr area = scale * scale;
        RDom tile(0, scale, 0, scale, "tile");
        Expr accumulator = cast<uint16_t>(input(scale * x + tile.x, scale * y + tile.y));
        output(x, y) = cast<uint8_t>(sum(accumulator) / area);
    }

    void schedule() {}

private:
    Var x{"x"}, y{"y"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Downsample, downsample)
