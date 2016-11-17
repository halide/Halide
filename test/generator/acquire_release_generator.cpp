#include "Halide.h"

namespace {

class AcquireRelease : public Halide::Generator<AcquireRelease> {
public:
    ImageParam input{ Float(32), 2, "input" };

    Func build() {
        Var x("x"), y("y");
        Func f("f");

        f(x, y) = input(x, y) * 2.0f + 1.0f;

        // Use the GPU for this f if a GPU is available.
        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var bx("bx"), by("by"), tx("tx"), ty("ty");
            f.gpu_tile(x, y, bx, by, tx, ty, 16, 16).compute_root();
        }
        return f;
    }
};

Halide::RegisterGenerator<AcquireRelease> register_my_gen{"acquire_release"};

}  // namespace
