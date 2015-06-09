#include "Halide.h"

namespace {

class EdgeDetect : public Halide::Generator<EdgeDetect> {
public:
    ImageParam input{ UInt(8), 2, "input" };

    Func build() {
        Var x, y;

        Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        // Gradients in x and y.
        Func gx("gx");
        Func gy("gy");
        gx(x, y) = cast<uint16_t>(clamped(x + 1, y)) - clamped(x - 1, y);
        gy(x, y) = cast<uint16_t>(clamped(x, y + 1)) - clamped(x, y - 1);

        Func result("result");

        result(x, y) = cast<uint8_t>(min(255, gx(x, y) * gx(x, y) + gy(x, y) * gy(x, y)));

        // CPU schedule:
        //   Parallelize over scan lines, 4 scanlines per task.
        //   Independently, vectorize in x.
        result
            .parallel(y, 4)
            .vectorize(x, natural_vector_size(UInt(8)));

        return result;
    }
};

Halide::RegisterGenerator<EdgeDetect> register_edge_detect{ "edge_detect" };

}  // namespace
