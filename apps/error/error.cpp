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
    /*
    Func f2("f2");
    f2(x) = x;
    f2(rx) += 5*rx;
    f2(rx) *= rx;
    */

    /* Referring to the bounds of a uniform image that isn't used */
    UniformImage input(Float(32), 3, "input");
    Func f3("f3");
    f3(x) = input.width();
    f3.compileToFile("f3");

    return 0;
}

