#include "Halide.h"

namespace {

class EdgeDetect : public Halide::Generator<EdgeDetect> {
public:
    Input<Buffer<uint8_t, 2>> input{"input"};
    Output<Buffer<uint8_t, 2>> result{"result"};

    void generate() {
        Var x, y;

        Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        // Upcast to 16-bit
        Func in16;
        in16(x, y) = cast<int16_t>(clamped(x, y));

        // Gradients in x and y.
        Func gx;
        Func gy;
        gx(x, y) = (in16(x + 1, y) - in16(x - 1, y)) / 2;
        gy(x, y) = (in16(x, y + 1) - in16(x, y - 1)) / 2;

        // Gradient magnitude.
        Func grad_mag;
        grad_mag(x, y) = (gx(x, y) * gx(x, y) + gy(x, y) * gy(x, y));

        // Draw the result
        result(x, y) = cast<uint8_t>(clamp(grad_mag(x, y), 0, 255));

        // CPU schedule:
        //   Parallelize over scan lines, 4 scanlines per task.
        //   Independently, vectorize in x.
        result
            .compute_root()
            .vectorize(x, 8)
            .parallel(y, 8);

        // Cope with rotated inputs
        input.dim(0).set_stride(Expr());
        result.specialize(input.dim(0).stride() == 1);
        result.specialize(input.dim(0).stride() == -1);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(EdgeDetect, edge_detect)
