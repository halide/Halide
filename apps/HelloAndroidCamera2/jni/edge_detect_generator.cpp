#include "Halide.h"

namespace {

class EdgeDetect : public Halide::Generator<EdgeDetect> {
public:
    ImageParam input{ UInt(8), 2, "input" };

    Func build() override {
        Var x, y;

        Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        // Gradients in x and y.
        Func gx("gx");
        Func gy("gy");
        gx(x, y) = (clamped(x + 1, y) - clamped(x - 1, y)) / 2;
        gy(x, y) = (clamped(x, y + 1) - clamped(x, y - 1)) / 2;

        Func result("result");

        result(x, y) = gx(x, y) * gx(x, y) + gy(x, y) * gy(x, y) + 128;

        // CPU schedule:
        //   Parallelize over scan lines, 4 scanlines per task.
        //   Independently, use vectors of size 32 in x.
        result.parallel(y, 4).vectorize(x, 8);

        // OpenCL schedule.
        // result.compute_root().vectorize(x, 8).gpu_tile(x, y, 2, 16,
        // Device_OpenCL);

        return result;
    }
};

Halide::RegisterGenerator<EdgeDetect> register_edge_detect{ "edge_detect" };

}  // namespace
