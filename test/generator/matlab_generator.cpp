#include "Halide.h"

using namespace Halide;

namespace {

class Matlab : public Halide::Generator<Matlab> {
public:
    ImageParam input{Float(32), 2, "input"};
    Param<float> scale{"scale"};
    Param<bool> negate{"negate"};

    Func build() {
        Var x, y;
        Func f("f");
        f(x, y) = input(x, y) * scale * select(negate, -1.0f, 1.0f);
        return f;
    }
};

Halide::RegisterGenerator<Matlab> register_matlab{"matlab"};

}  // namespace
