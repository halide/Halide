#include "Halide.h"

namespace {

class AcquireRelease : public Halide::Generator<AcquireRelease> {
public:
    ImageParam input{ Float(32), 2, "input" };

    static std::string name() {
        return "acquire_release";
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
};

Halide::RegisterGenerator<AcquireRelease> register_my_gen;

}  // namespace
