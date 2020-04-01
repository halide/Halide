#include <type_traits>

#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = x;

    // Hide the previous definition.
    f(x) = 2;

    return 0;
}
