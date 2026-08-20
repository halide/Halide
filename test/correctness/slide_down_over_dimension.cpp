// Sliding over a split dimension gives wrong answers when the window slides
// downwards. Reduced from sliding-window fuzzer seed 17688697638350579054.
//
// The consumer reads the producer at reversed coordinates, so the region
// required moves towards lower indices as the loops advance, and the window
// slides down rather than up. The select makes the region required jump
// between the first and second iteration and move by one after that.
//
// Sliding over x, which is a loop, is fine. Sliding over y, which has been
// split and so survives only as a let, is what goes wrong. Named for what it
// needs: a dimension rather than a loop, and a window that slides down.

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    const int W = 8, H = 8;
    Var x("x"), y("y"), yo("yo"), yi("yi");

    Func in("in"), p("p"), out("out");
    in(x, y) = cast<uint8_t>(x * 3 + y);
    p(x, y) = in(x, y) + in(x + 1, y) + in(x, y + 1);
    Expr yy = select(y < 1, 0, 2) + y;
    out(x, y) = p(15 - x, 16 - yy) + p(16 - x, 16 - yy);

    in.compute_root();
    out.split(y, yo, yi, 4, TailStrategy::GuardWithIf);
    p.store_at(out, Var::outermost()).compute_at(out, x).slide(out, y);

    Buffer<uint8_t> got = out.realize({W, H});

    auto inv = [](int i, int j) { return (uint8_t)(i * 3 + j); };
    auto pv = [&](int i, int j) {
        return (uint8_t)(inv(i, j) + inv(i + 1, j) + inv(i, j + 1));
    };

    int bad = 0;
    for (int j = 0; j < H; j++) {
        for (int i = 0; i < W; i++) {
            int jj = (j < 1 ? 0 : 2) + j;
            uint8_t want = (uint8_t)(pv(15 - i, 16 - jj) + pv(16 - i, 16 - jj));
            if (got(i, j) != want) {
                if (bad < 4) {
                    printf("out(%d, %d) = %d instead of %d\n", i, j, got(i, j), want);
                }
                bad++;
            }
        }
    }
    if (bad) {
        printf("%d wrong values\n", bad);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
