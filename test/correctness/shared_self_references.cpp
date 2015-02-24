#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Check that recursive references get tracked properly
    {
        Func f;
        Var x;
        f(x) = x;
        {
            Expr e = f(2);
            f(0) = e;
            f(1) = e;
        } // Destroy e
    } // Destroy f

    // f should have been cleaned up. valgrind will complain if it
    // hasn't been.

    printf("Success!\n");
    return 0;
}
