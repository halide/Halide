#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = cast<uint8_t>(0);
    hist(im(r)) += cast<uint8_t>(1);

    hist.compute_root();

    RVar ro, ri;
    hist.update()
        .atomic()
        .split(r, ro, ri, 8)
        .gpu_blocks(ro)
        .gpu_threads(ri);

    // GPU doesn't support 8/16-bit atomics
    Realization out = hist.realize(hist_size);
    return 0;
}
