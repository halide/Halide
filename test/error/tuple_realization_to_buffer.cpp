#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = {x, x, x};

    Buffer<int> buf = f.realize({1024});

    printf("Success!\n");
    return 0;
}
