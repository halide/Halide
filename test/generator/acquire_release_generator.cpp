#include "Halide.h"

using namespace Halide;

namespace {

class AcquireRelease : public Generator<AcquireRelease> {
public:
    ImageParam input{ Float(32), 2, "input" };

    Func build() override {
        Var x("x"), y("y");
        Func f("f");

        f(x, y) = input(x, y) * 2.0f + 1.0f;

        // Use the GPU for this f if a GPU is available.
        Target target = get_target();
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, 16, 16).compute_root();
        }
        return f;
    }
};

RegisterGenerator<AcquireRelease> register_my_gen("acquire_release");

}  // namespace
