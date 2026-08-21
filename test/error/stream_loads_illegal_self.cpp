#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x;

    f(x) = x;

    // A Stage can't stream loads of itself: naming f's own Stage in its own
    // stream_loads() request is illegal.
    f.stream_loads({f});

    printf("Success!\n");
    return 0;
}
