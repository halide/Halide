#include "Halide.h"

using namespace Halide;

namespace {

class GpuOnly : public Generator<GpuOnly> {
public:
    ImageParam input{ Int(32), 2, "input" };

    Func build() override {
        Var x("x"), y("y");

        // Create a simple pipeline that scales pixel values by 2.
        Func f("f");
        f(x, y) = input(x, y) * 2;

        Target target = get_target();
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, 16, 16);
        }
        return f;
    }
};

RegisterGenerator<GpuOnly> register_my_gen("gpu_only");

}  // namespace
