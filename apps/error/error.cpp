#include <Halide.h>
using namespace Halide;

#include "../png.h"

int main(int argc, char **argv) {
    Var x("x"), y("y");
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
    /* NO LONGER CONSIDERED AN ERROR
    UniformImage input(Float(32), 3, "input");
    Func f3("f3");
    f3(x) = input.width();
    f3.compileToFile("f3");
    */

    /* Using a different number of arguments in the initialize and the update */
    Func f4("f4");
    Func f5("f5");
    f5(x, y) = x+y;
    f4(x) = 0;
    f4(rx) = f4(rx) + f5(rx);
    f4.compileToFile("f4");

    return 0;
}

