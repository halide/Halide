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

        return f;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(GpuObjectLifetime, gpu_object_lifetime)
