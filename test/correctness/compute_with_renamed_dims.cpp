#include "Halide.h"
#include <algorithm>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

// Two Funcs over differently named pure Vars fuse at the dims a schedule
// gives the same names (by split and rename).
int main(int argc, char **argv) {
    Var x("x"), y("y"), u("u"), v("v"), xo("xo"), xi("xi"), r("r");
    Func f("f"), g("g"), out("out");
    f(x, y) = x + 2 * y;
    g(u, v) = 3 * u - v;
    out(x, y) = f(x, y) + g(x / 4, y);

    f.compute_root().rename(y, r).split(x, xo, xi, 8);
    g.compute_root().rename(v, r).split(u, xo, xi, 2).compute_with(f, xo);

    // g's xo and r are f's loops: g has a loop of its own only over xi
    std::vector<std::string> g_loops;
    Module m = Pipeline(out).compile_to_module({}, "out", get_jit_target_from_environment());
    for (const LoweredFunc &lf : m.functions()) {
        visit_with(lf.body, [&](auto *self, const For *op) {
            if (starts_with(op->name, "g.") && std::find(g_loops.begin(), g_loops.end(), op->name) == g_loops.end()) {
                g_loops.push_back(op->name);
            }
            self->visit_base(op);
        });
    }
    if (g_loops.size() != 1 || g_loops[0] != "g.s0.u.xi") {
        printf("g's loops not fused with f's:");
        for (const std::string &l : g_loops) {
            printf(" %s", l.c_str());
        }
        printf("\n");
        return 1;
    }

    Buffer<int> o = out.realize({64, 16});
    for (int j = 0; j < o.height(); j++) {
        for (int i = 0; i < o.width(); i++) {
            const int correct = i + 2 * j + 3 * (i / 4) - j;
            if (o(i, j) != correct) {
                printf("out(%d, %d) = %d instead of %d\n", i, j, o(i, j), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
