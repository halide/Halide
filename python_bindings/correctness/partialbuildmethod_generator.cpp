#include "Halide.h"

namespace {

#ifdef HALIDE_ALLOW_GENERATOR_BUILD_METHOD
// This Generator exists solely to test converted old-style
// generators -- which use Input<> rather than Param/ImageParam, but *don't* use
// Output<>/generate().
//
// Do not convert it to new-style until/unless we decide to entirely remove
// support for those Generators.
class PartialBuildMethod : public Halide::Generator<PartialBuildMethod> {
public:
    GeneratorParam<float> compiletime_factor{"compiletime_factor", 1, 0, 100};

    Input<Buffer<float, 2>> input{"input"};
    Input<float> runtime_factor{"runtime_factor", 1.0};

    Func build() {
        Var x, y;

        Func g;
        g(x, y) =
            cast<int32_t>(input(x, y) * compiletime_factor * runtime_factor);
        return g;
    }
};
#else
// Provide a placeholder here that uses generate(), just to allow this test to
// succeed even if build() is disabled.
class PartialBuildMethod : public Halide::Generator<PartialBuildMethod> {
public:
    GeneratorParam<float> compiletime_factor{"compiletime_factor", 1, 0, 100};

    Input<Buffer<float, 2>> input{"input"};
    Input<float> runtime_factor{"runtime_factor", 1.0};
    Output<Buffer<int32_t, 2>> output{"output"};

    void generate() {
        Var x, y;

        output(x, y) = cast<int32_t>(input(x, y) * compiletime_factor * runtime_factor);
    }
};
#endif

}  // namespace

HALIDE_REGISTER_GENERATOR(PartialBuildMethod, partialbuildmethod)
