#include "Halide.h"

using namespace Halide;

namespace {

class GpuObjectLifetime : public Generator<GpuObjectLifetime> {
public:
    Func build() override {
        Var x;

        Func f;
        f(x) = x;

        Target target = get_target();
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, 16);
        }
        // The test requires gpu_debug to examine the output.
        target.set_feature(Target::Debug);
        return f;
    }
};

RegisterGenerator<GpuObjectLifetime> register_my_gen("gpu_object_lifetime");

}  // namespace
