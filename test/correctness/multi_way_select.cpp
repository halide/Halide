#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
#if defined(__APPLE__) && defined(__x86_64__)
    if (get_jit_target_from_environment().has_feature(Target::WebGPU)) {
        printf("[SKIP] This fails on x86 Macs (pre-Ventura) due to a bug in Apple's Metal Shading Language compiler. See https://github.com/halide/Halide/issues/7389.\n");
        return 0;
    }
#endif

    Func f;
    Var x;

    int cases[] = {3, 7, 24, 5, 37, 91, 33, 14};

    f(x) = select(x == 0, cases[0],
                  x == 1, cases[1],
                  x == 2, cases[2],
                  x == 3, cases[3],
                  x == 4, cases[4],
                  x == 5, cases[5],
                  x == 6, cases[6],
                  cases[7]);

    Func g;
    g(x) = 0;
    for (int i = 0; i < 8; i++) {
        g(i) = cases[i];
    }

    RDom r(0, 8);
    uint32_t err = evaluate_may_gpu<uint32_t>(sum(abs(g(r) - f(r))));

    if (err != 0) {
        printf("Multi-way select didn't equal equivalent reduction!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
