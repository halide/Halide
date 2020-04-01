#include <type_traits>

#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x;
    Buffer<float> im = f.realize(100);

    return 0;
}
