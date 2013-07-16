#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    Image<int> input(5, 5);
    Func f;
    f(x, y) = input(x, y) * 2; 
    f.vectorize(x, 1);

    // Should result in an error
    Image<int> out = f.realize(5, 5);

    printf("Success!\n");
    return 0;
}
