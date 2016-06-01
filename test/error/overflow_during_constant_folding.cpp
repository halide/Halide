#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = Expr(0x12345678) * Expr(0x76543210);

    f.realize(10);

    return 0;
}
