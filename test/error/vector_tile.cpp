#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Test reporting of mismatched sizes error in vector-of-strategies variant
    Var i, j;

    Func f;
    f(i, j) = i * j;

    Var io, jo;
    // Should result in an error
    // Bad because the vector lengths don't match
    f.tile({i, j}, {io, jo}, {i, j}, {8, 8}, {TailStrategy::RoundUp, TailStrategy::RoundUp, TailStrategy::RoundUp});

    printf("Success!\n");
    return 0;
}
