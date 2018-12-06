#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // This tests a temporary hack to silence the error when you try
    // to unroll a loop of non-constant size. We have yet to figure
    // out whether or how to expose this behavior in the scheduling
    // language.
    Func f;
    Var x;
    f(x) = 3;

    // Would normally cause an error, because x doesn't have a known
    // constant size.
    f.unroll(x);


    setenv("HL_PERMIT_FAILED_UNROLL", "1", 1);
    f.realize(17);
}
