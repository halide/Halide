#include "Halide.h"

namespace {

// This Generator exists solely to verify that old-style generators (using the
// Param/ImageParam/build() method, rather than Input<>/Output<>/generate()/schedule()) still work.
// Do not convert it to new-style until/unless we decide to entirely remove support
// for those Generators.
class BuildMethod : public Halide::Generator<BuildMethod> {
public:
    GeneratorParam<float> compiletime_factor{ "compiletime_factor", 1, 0, 100 };

    ImageParam input{Halide::Float(32), 3, "input"};
    Param<float> runtime_factor{ "runtime_factor", 1.0 };

    Func build() {
        Var x, y, c;

        Func g;
        g(x, y, c) = cast<int32_t>(input(x, y, c) * compiletime_factor * runtime_factor);
        return g;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(BuildMethod, buildmethod)
