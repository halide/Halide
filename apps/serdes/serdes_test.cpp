#include <Halide.h>

#include "halide_ir_generated.h"

using namespace Halide;

int main(int argc, char* argv[]) {
    Halide::Func gradient;

    Halide::Var x, y;

    Halide::Expr e = x + y;

    gradient(x, y) = e;

    Halide::Buffer<int32_t> output = gradient.realize({800, 600});

    return 0;
}