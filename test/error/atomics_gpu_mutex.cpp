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

    RVar ro, ri;
    f.update()
        .atomic()
        .split(r, ro, ri, 8)
        .gpu_blocks(ro)
        .gpu_threads(ri);

    // hist's update will be lowered to mutex locks,
    // and we don't allow GPU blocks on mutex locks since
    // it leads to deadlocks.
    // This should throw an error
    Realization out = f.realize(img_size);

    printf("Success!\n");
    return 0;
}
