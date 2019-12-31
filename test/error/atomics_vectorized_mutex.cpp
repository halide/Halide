#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = Tuple(0, 0);
    hist(im(r)) += Tuple(1, 2);

    hist.compute_root();

    RVar ro, ri;
    hist.update()
        .atomic()
        .split(r, ro, ri, 8)
        .parallel(ro)
        .vectorize(ri);

    // hist's update will be lowered to mutex locks,
    // and we don't allow vectorization on mutex locks since
    // it leads to deadlocks.
    // This should throw an error
    Realization out = hist.realize(hist_size);
    return 0;
}
