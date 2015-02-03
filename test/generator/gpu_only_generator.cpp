#include "Halide.h"

namespace {

class GpuOnly : public Halide::Generator<GpuOnly> {
public:
    ImageParam input{ Int(32), 2, "input" };

    void help(std::ostream &out) override {
        out << "This tests a filter that runs code on the GPU only and makes sure the "
            << "memory management of the inputs and outputs is correct (e.g. the dirty "
            << "bits come out right).\n";
    }

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

    bool test() override {
        // AOT-only test
        return true;
    }
};

Halide::RegisterGenerator<GpuOnly> register_my_gen{"gpu_only"};

}  // namespace
