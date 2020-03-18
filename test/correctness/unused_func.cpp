#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, xi, yi;

    ImageParam input(Float(32), 2);

    Func filtered;
    filtered(x, y) = input(x, y);
    filtered.compute_root();

    Func false_func;
    false_func() = cast<bool>(0);

    Func result;
    result(x, y) = select(false_func(), filtered(x, y), input(0, 0));

    // The bounds required on the input depend on filtered, but
    // filtered is not going to be computed because it simplified away
    // entirely. This test ensures things compile anyway.
    result.compile_jit();

    return 0;
}
