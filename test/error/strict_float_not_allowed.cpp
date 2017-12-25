#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t("host");
    Func f;
    Var x;
    f(x) = strict_float(cast<float>(x) * 42.0f);

    (void)f.realize(42, t);
    
    printf("I should not have reached here\n");

    return 0;
}
