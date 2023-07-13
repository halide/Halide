#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f[2];
    Var x, y;
    RDom r(0, 12, 0, 10);

    RDom r2(0, 12);
    for (int i = 0; i < 2; i++) {
        f[i](x, y) = x + y;
        // All of these update definitions should be safe to
        // parallelize, because there's never an overlap between the
        // writes done by one thread and the reads and writes done by
        // any other thread.

        f[i](r.x, r.y) += 1;
        f[i](r.x, r.y) += f[i](r.x + 20, r.y);

        f[i](2 * r2 + 1, 0) += f[i](2 * r2, 0);
        f[i](r2, 0) += f[i](r2 - 1, 1);
    }

    f[0].compute_root();
    RVar rxo, ryo, rxi, ryi, rt;
    f[0].update(0).tile(r.x, r.y, rxo, ryo, rxi, ryi, 4, 2).fuse(rxo, ryo, rt).parallel(rt);
    f[0].update(1).parallel(r.x).parallel(r.y).unroll(r.y, 2);
    f[0].update(2).vectorize(r2, 4).unroll(r2);
    f[0].update(3).parallel(r2, 4);
    f[1].compute_root();

    RDom r_check(0, 20, 0, 20);
    int error = evaluate<int>(sum(f[0](r_check.x, r_check.y) - f[1](r_check.x, r_check.y)));

    if (error != 0) {
        printf("Serial version did not match parallel version\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
