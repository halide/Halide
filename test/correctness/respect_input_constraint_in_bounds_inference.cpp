#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam im(Float(32), 1);
    Func f, g;
    Var x;

    f(x) = x;
    g(x) = f(x % im.dim(0).extent());
    im.dim(0).set_extent(16);

    // Given the constraint, we know the bounds of f should be less than 16, so
    // the compiler should be happy placing it in a register. This is just a way
    // to assert that the size of the allocation has been statically determined.
    f.compute_root().store_in(MemoryType::Register);

    g.compile_jit();

    printf("Success!\n");
    return 0;
}
