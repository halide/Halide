#include "Halide.h"

using namespace Halide;

namespace {

class Matlab : public Halide::Generator<Matlab> {
public:
    Input<Buffer<float>> input{"input", 2};
    Input<float>         scale{"scale"};
    Input<bool>          negate{"negate"};

    Func build() {
        Var x, y;
        Func f("f");
        f(x, y) = input(x, y) * scale * select(negate, -1.0f, 1.0f);
        return f;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Matlab, matlab)
