#include "Halide.h"

using namespace Halide;

int main(int argc, char *argv[]) {
    // This is the atomic version of issue 4283,
    // where I believe should be running fine after we fix
    // that issue.
    int img_size = 1000;
    int hist_size = 53;

    Func im, hist, final;
    Var x, y;
    RDom r(0, img_size);

    im(x) = (x*x) % hist_size;

    hist(x) = cast<T>(0);
    hist(im(r)) += cast<T>(1);

    final(x, y) = hist((x + y) % hist_size);

    Type t = cast<T>(0).type();
    bool is_float_16 = t.is_float() && t.bits() == 16;

    final.compute_root().parallel(y);
    hist.store_at(final, y)
        .compute_at(final, x);
    RVar ro, ri;
    hist.update()
        .atomic()
        .split(r, ro, ri, 32)
        .gpu_blocks(ro)
        .gpu_threads(ri);

    Buffer<T> correct_hist(hist_size);
    Buffer<T> correct_final(10, 10);
    correct_hist.fill(T(0));
    correct_final.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i*i) % hist_size;
        correct_hist(idx) = correct_hist(idx) + T(1);
    }
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            correct_final(i, j) = correct_hist((i + j) % hist_size);
        }
    }

    // Run 100 times to make sure race condition do happen
    for (int iter = 0; iter < 100; iter++) {
        Buffer<T> out = final.realize(10, 10);
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10; j++) {
                check(__LINE__, out(i, j), correct_final(i, j));
            }
        }
    }
    return 0;
}
