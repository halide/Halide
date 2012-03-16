#include <Halide.h>
using namespace Halide;

#include "../png.h"

int main(int argc, char **argv) {
    Var x("x");
    RVar rx(0, 100, "rx");

    /* Multiple definitions */
    /*
    Func f1("f1");
    f1(x) = x;
    f1(x) = x*2;
    */

    /* Add more than one update step to a reduction */
    Func f2("f2");
    f2(x) = x;
    f2(rx) += 5*rx;
    f2(rx) *= rx;

    return 0;
}

