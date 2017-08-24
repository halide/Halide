#include "Halide.h"

namespace {

class GpuOnly : public Halide::Generator<GpuOnly> {
public:
    ImageParam input{ Int(32), 2, "input" };

    Func build() {
        Var x("x"), y("y");

        // Create a simple pipeline that scales pixel values by 2.
        Func f("f");
        f(x, y) = input(x, y) * 2;

        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var xo, yo, xi, yi;
            f.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        }
        return f;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(GpuOnly, gpu_only)
