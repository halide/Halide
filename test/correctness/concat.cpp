#include "Halide.h"

using namespace Halide;

int count[2];
extern "C" HALIDE_EXPORT_SYMBOL int call_counter(int slot, int val) {
    count[slot]++;
    return val;
}
HalideExtern_2(int, call_counter, int, int);

int main(int argc, char **argv) {
    count[0] = count[1] = 0;

    Func f, g, h;
    Var x;

    f(x) = call_counter(0, x + 1);
    g(x) = call_counter(1, x + 2);
    h(x) = select(x < 100, f(x), g(x));

    // While f and g are loaded over the entire range of h, f only
    // needs to be correct where x < 100, and g only needs to be
    // correct where x >= 100, so there should be a mismatch between
    // bounds computed and bounds allocated.

    f.compute_root();
    g.compute_root();
    h.compute_root();

    Buffer<int> buf = h.realize({200});

    for (int i = 0; i < 200; i++) {
        int correct = i < 100 ? i + 1 : i + 2;
        if (buf(i) != correct) {
            printf("buf(%d) = %d instead of %d\n", i, buf(i), correct);
            return 1;
        }
    }

    if (count[0] != 100 || count[1] != 100) {
        printf("Incorrect counts: %d %d\n", count[0], count[1]);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
