#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    int img_size = 10000;

    Func f;
    Var x;
    RDom r(0, img_size);

    f(x) = Tuple(1, 0);
    f(r) = Tuple(f(r)[1] + 1, f(r)[0] + 1);

    f.compute_root();

    f.update()
        .atomic()
        .vectorize(r, 8);

    // f's update will be lowered to mutex locks,
    // and we don't allow vectorization on mutex locks since
    // it leads to deadlocks.
    // This should throw an error
    Realization out = f.realize(img_size);

    printf("Success!\n");
    return 0;
}
