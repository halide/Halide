#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    // You can't use the same variable more than once in the LHS of a
    // pure definition.
    f(x, x) = x;

    return 0;
}
