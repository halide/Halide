#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Expr undef_min, undef_extent;

    // This should assert-fail
    RDom r(undef_min, undef_min);

    // Just to ensure compiler doesn't optimize-away the RDom ctor
    printf("Dimensions: %d\n", r.dimensions());

    printf("Success!\n");
    return 0;
}
