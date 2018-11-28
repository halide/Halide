#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;

    g.define_extern("foo", {}, UInt(16), 3);

    // Show throw an error immediately because g was defined with 3 dimensions.
    f(x, y) = cast<float>(g(x, y));

    return 0;
}
