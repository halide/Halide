#include "Halide.h"

namespace {

class GpuObjectLifetime : public Halide::Generator<GpuObjectLifetime> {
public:
    Func build() {
        Var x;

        Func f;
        f(x) = x;

        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var xo, xi;
            f.gpu_tile(x, xo, xi, 16);
        }
        // The test requires gpu_debug to examine the output.
        target.set_feature(Target::Debug);
        return f;
    }
};

Halide::RegisterGenerator<GpuObjectLifetime> register_my_gen{"gpu_object_lifetime"};

}  // namespace
