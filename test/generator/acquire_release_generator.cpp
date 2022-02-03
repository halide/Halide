#include "Halide.h"

#if HALIDE_PREFER_G2_GENERATORS

namespace {

using namespace Halide;

Func AcquireRelease(const Target &target, Func input) {
    Var x("x"), y("y");

    Func output("output");
    output(x, y) = input(x, y) * 2.0f + 1.0f;

    // Use the GPU for this f if a GPU is available.
    if (target.has_gpu_feature()) {
        Var bx("bx"), by("by"), tx("tx"), ty("ty");
        output.gpu_tile(x, y, bx, by, tx, ty, 16, 16).compute_root();
    }

    return output;
}

}  // namespace

HALIDE_REGISTER_G2(
    AcquireRelease,   // actual C++ fn
    acquire_release,  // build-system name
    Target(),
    Input("input", Float(32), 2),
    Output("output", Float(32), 2))

#else  // not HALIDE_PREFER_G2_GENERATORS

namespace {

class AcquireRelease : public Halide::Generator<AcquireRelease> {
public:
    Input<Buffer<float, 2>> input{"input"};
    Output<Buffer<float, 2>> output{"output"};

    void generate() {
        Var x("x"), y("y");

        output(x, y) = input(x, y) * 2.0f + 1.0f;

        // Use the GPU for this f if a GPU is available.
        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var bx("bx"), by("by"), tx("tx"), ty("ty");
            output.gpu_tile(x, y, bx, by, tx, ty, 16, 16).compute_root();
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(AcquireRelease, acquire_release)

#endif
