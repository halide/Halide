#include "Halide.h"

using namespace Halide;

class Demo : public Generator<Demo> {
public:
    Input<Buffer<float>> input{"input", 2};
    Output<Buffer<float>> output{"output", 2};

    void generate() {
        Var x, y;

        Func f;
        f(x, y) = input(x-1, y) + input(x, y+1);
        f(x, y) += 13;

        output(x, y) = f(x/2, y/2);

        output.estimate(x, 0, 1024).estimate(y, 0, 1024);
    }
};

HALIDE_REGISTER_GENERATOR(Demo, demo);
