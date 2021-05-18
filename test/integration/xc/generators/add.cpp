#include <Halide.h>
using namespace Halide;

struct Add : Generator<Add> {
    Output<Buffer<int32_t>> output{"output", 2};

    void generate() {
        Var x, y;
        output(x, y) = x + y;

        output.set_estimates({{0, 64}, {0, 64}});
    }
};

HALIDE_REGISTER_GENERATOR(Add, add);
