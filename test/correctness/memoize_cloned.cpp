#include "Halide.h"

using namespace Halide;

int call_count;
extern "C" HALIDE_EXPORT_SYMBOL int call_counter(int x) {
    call_count++;
    return x;
}
HalideExtern_1(int, call_counter, int);

int main(int argc, char **argv) {
    Func f, g, h;
    Var x, y;

    // A clone should use the same cache key as the parent, so that
    // computations of the clone can reuse computations of the
    // parent. This pipeline exploits that to compute f per row of one
    // consumer, then retrieve it from cache per row of another
    // consumer.
    //
    // Setting cache size gives you a trade-off between peak memory
    // usage and recompute.

    f(x, y) = call_counter(x);
    g(x, y) = f(x, y) * 2;
    h(x, y) = f(x, y) + g(x, y);

    h.compute_root();
    g.compute_root();
    f.clone_in(h).compute_at(h, y).memoize();
    f.compute_at(g, y).memoize();

    h.bound(x, 0, 1024).bound(y, 0, 32);

    call_count = 0;
    h.realize({1024, 32});
    if (call_count != 1024 * 32) {
        printf("call_count was supposed to be 1024 * 32: %d\n", call_count);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
