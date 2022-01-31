#include "Halide.h"

namespace {

// This Generator exists solely to compare the output with BuildMethod and PartialBuildMethod.
class NoBuildMethod : public Halide::Generator<NoBuildMethod> {
public:
    GeneratorParam<float> compiletime_factor{"compiletime_factor", 1, 0, 100};

    Input<Buffer<float, 2>> input{"input"};
    Input<float> runtime_factor{"runtime_factor", 1.0};
    Output<Buffer<int32_t, 2>> output{"output"};

    void generate() {
        Var x, y;

        output(x, y) =
            cast<int32_t>(input(x, y) * compiletime_factor * runtime_factor);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(NoBuildMethod, nobuildmethod)
