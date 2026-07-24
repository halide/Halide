#include "Halide.h"
using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, y, fused;

    f(x, y) = x + y;
    f.fuse(x, x, fused);

    printf("Success!\n");
    return 0;
}
