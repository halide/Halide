#include "Halide.h"

using namespace Halide;

namespace {

class Matlab : public Halide::Generator<Matlab> {
public:
    Input<Buffer<float, 2>> input{"input"};
    Input<float> scale{"scale"};
    Input<bool> negate{"negate"};
    Output<Buffer<float, 2>> output{"output"};

    void generate() {
        Var x, y;
        output(x, y) = input(x, y) * scale * select(negate, -1.0f, 1.0f);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Matlab, matlab)
