#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    RDom r(0, 100);

    f(x) = 0;
    f(clamp(f(r), 0, 100)) = f(r) + 1;

    f.compute_root();
    f.update()
        .atomic(true /* override_associativity_test */)
        .parallel(r);

    // f references itself on the index, making the atomic illegal.
    Realization out = f.realize(100);
    return 0;
}
