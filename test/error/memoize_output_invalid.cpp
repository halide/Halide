#include <Halide.h>
using namespace Halide;

int main(int argc, char **argv) {
    Var x{"x"};
    Func f{"f"};
    f(x) = 0.0f;
    f(x) += 1;
    f.memoize();

    f.realize({3});
}
