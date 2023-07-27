#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;
    Func f;
    f(x) = x;
    f.split(x, x, x, 16, TailStrategy::RoundUp);

    printf("Success!\n");
    return 0;
}
