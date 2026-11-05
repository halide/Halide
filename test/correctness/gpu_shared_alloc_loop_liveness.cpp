#include "Halide.h"

using namespace Halide;

// A shared allocation made outside a serial loop at the GPU block level and
// read inside it is live for the whole loop: one made later in the loop body
// must not reuse its memory, or the next iteration reads clobbered values.
int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Var x, y, xo, xi;
    RDom r(0, 8);
    Func t("t"), p("p"), q("q"), out("out");
    t(x) = x * x + 3;
    p(x, y) = t((x + y) & 15);
    q(x, y) = p(x, y) * 2 + y;
    out(x) = 0;
    out(x) += q(x, r);

    out.gpu_tile(x, xo, xi, 16);
    out.update().split(x, xo, xi, 16).reorder(xi, r, xo).gpu_blocks(xo).gpu_threads(xi);
    // t is computed once per block, outside the loop over r. p and q are
    // computed per iteration of it, q after p's last read of t.
    t.compute_at(out, xo).gpu_threads(x);
    p.compute_at(out, r).gpu_threads(x);
    q.compute_at(out, r).gpu_threads(x);

    Buffer<int> result = out.realize({64});
    for (int i = 0; i < 64; i++) {
        int correct = 0;
        for (int j = 0; j < 8; j++) {
            int k = (i + j) & 15;
            correct += (k * k + 3) * 2 + j;
        }
        if (result(i) != correct) {
            printf("result(%d) = %d instead of %d\n", i, result(i), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
