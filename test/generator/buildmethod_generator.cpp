#include "Halide.h"

namespace {

#ifdef HALIDE_ALLOW_GENERATOR_BUILD_METHOD
// This Generator exists solely to test old-style generators (using the
// build() method, rather than generate()/schedule()).
// Do not convert it to new-style until/unless we decide to entirely remove support
// for those Generators.
class BuildMethod : public Halide::Generator<BuildMethod> {
public:
    GeneratorParam<float> compiletime_factor{"compiletime_factor", 1, 0, 100};

    Input<Buffer<float, 3>> input{"input"};
    Input<float> runtime_factor{"runtime_factor", 1.0};

    Func build() {
        Var x, y, c;

        Func g;
        g(x, y, c) = cast<int32_t>(input(x, y, c) * compiletime_factor * runtime_factor);
        return g;
    }
};
#else
// Provide a placeholder here that uses generate(), just to allow this test to
// succeed even if build() is disabled.
class BuildMethod : public Halide::Generator<BuildMethod> {
public:
    GeneratorParam<float> compiletime_factor{"compiletime_factor", 1, 0, 100};

    Input<Buffer<float, 3>> input{"input"};
    Input<float> runtime_factor{"runtime_factor", 1.0};
    Output<Buffer<int32_t, 3>> output{"output"};

    void generate() {
        Var x, y, c;

        output(x, y, c) = cast<int32_t>(input(x, y, c) * compiletime_factor * runtime_factor);
    }
};
#endif

}  // namespace

HALIDE_REGISTER_GENERATOR(BuildMethod, buildmethod)
