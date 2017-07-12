#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    const int N = 20;

    Func f;
    Var x, y;
    f(x, y) = x + y;
    for (int i = 0; i < N; i++) {
        f(x, i) += 1;
        f(i, y) += 1;
    }
    f.compute_root();

    Buffer<int> im = f.realize(N, N);

    printf("Success!\n");
    return 0;

}
