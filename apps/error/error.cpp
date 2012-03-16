#include <Halide.h>
using namespace Halide;

#include "../png.h"

int main(int argc, char **argv) {

    /* Multiple definitions */
    Func f1("f1");
    Var x;
    f1(x) = x;
    f1(x) = x*2;

    f1.compileToFile("f1");    

    return 0;
}

