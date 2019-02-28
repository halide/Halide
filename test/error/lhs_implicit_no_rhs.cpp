#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, z;
    Halide::Func f, g, h;
    g(x, y) = x * y;
    h(x, y, z) = x * y * z;
    f(x, y, z, _) = g(x, y) + h(x, y, z);
    return 0;
}
