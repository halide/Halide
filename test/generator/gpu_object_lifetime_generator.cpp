#include "Halide.h"

namespace {

class GpuObjectLifetime : public Halide::Generator<GpuObjectLifetime> {
public:
    void help(std::ostream &out) override {
        out << "This tests that GPU buffers are acquired and released at appropriate times.\n";
    }

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

    bool test() {
        // This is an AOT-only test.
        return true;
    };
};

Halide::RegisterGenerator<GpuObjectLifetime> register_my_gen{"gpu_object_lifetime"};

}  // namespace
