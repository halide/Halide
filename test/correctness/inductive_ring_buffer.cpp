// A Func that is defined in terms of itself, holds a Tuple, and is ring
// buffered. Each of those three with any one of the others works; all three
// together used to fail an internal assertion in SplitTuples, comparing the
// arguments of a call against those of a store.
//
// Ring buffering gives the store an extra dimension for the ring index. The
// values being stored were passed through untouched, so a call the Func makes
// to itself - which only happens when it is defined in terms of itself - kept
// the arity it had before, and the two no longer agreed.
#include "Halide.h"
#include <cstdio>
using namespace Halide;
int main() {
    Var x("x"), y("y");
    Func f({Int(32), Int(32)}, "f"), g("g");
    // component 0 counts up from y; component 1 sums component 0 so far.
    f(x, y) = select(x <= 0, Tuple(y, 0),
                     Tuple(f(x - 1, y)[0] + 1, f(x - 1, y)[1] + f(x - 1, y)[0]));
    g(x, y) = f(x, y)[1];
    f.hoist_storage_root().compute_at(g, x).ring_buffer(2);
    g.bound(x, 0, 12).bound(y, 0, 4);
    Buffer<int> im = g.realize({12, 4});
    for (int j = 0; j < 4; j++) {
        int c = j, s = 0;
        for (int i = 0; i < 12; i++) {
            if (i > 0) { s += c; c += 1; }
            if (im(i, j) != s) { printf("bad %d %d: %d != %d\n", i, j, im(i, j), s); return 1; }
        }
    }
    printf("Success!\n");
    return 0;
}
