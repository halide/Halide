#include "Halide.h"

namespace {

// This Generator exists solely to test converted old-style
// generators -- which use Input<> rather than Param/ImageParam, but *don't* use
// Output<>/generate().
//
// Do not convert it to new-style until/unless we decide to entirely remove
// support for those Generators.
class PartialBuildMethod : public Halide::Generator<PartialBuildMethod> {
 public:
  GeneratorParam<float> compiletime_factor{"compiletime_factor", 1, 0, 100};

  Input<Buffer<float>> input{"input", 2};
  Input<float> runtime_factor{"runtime_factor", 1.0};

  Func build() {
    Var x, y;

    Func g;
    g(x, y) =
        cast<int32_t>(input(x, y) * compiletime_factor * runtime_factor);
    return g;
  }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(PartialBuildMethod, partialbuildmethod)
