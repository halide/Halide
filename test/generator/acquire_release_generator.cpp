#include "Halide.h"

namespace {

class AcquireRelease : public Halide::Generator<AcquireRelease> {
public:
    ImageParam input{ Float(32), 2, "input" };

    void help(std::ostream &out) override {
        out << "This tests that custom acquire and release context functions work for CUDA and OpenCL\n";
    }

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

    bool test() override {
        // This is an AOT-only test.
        return true;
    }
};

Halide::RegisterGenerator<AcquireRelease> register_my_gen{"acquire_release"};

}  // namespace
