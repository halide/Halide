#include "Halide.h"
#include <cstring>
#include <string>

using namespace Halide;

// A blend (RoundUpAndBlend / ShiftInwardsAndBlend) loads the value its store
// would overwrite. If an earlier PredicateStores split guarded the store's
// coordinates, bounds inference trusts that guard for the load too and sizes
// the allocation to the realized region, but the load used to run (and read
// out of bounds) where the store was predicated off.

namespace {

std::string traced;
int realization[4];
int bad_loads = 0;
bool in_produce = false;

int32_t trace_fn(JITUserContext *, const halide_trace_event_t *e) {
    if (traced != e->func) {
        return 0;
    }
    if (e->event == halide_trace_begin_realization) {
        memcpy(realization, e->coordinates, sizeof(realization));
    } else if (e->event == halide_trace_produce) {
        in_produce = true;
    } else if (e->event == halide_trace_end_produce) {
        in_produce = false;
    } else if (e->event == halide_trace_load && in_produce) {
        int lanes = e->lanes;
        for (int lane = 0; lane < lanes; lane++) {
            for (int d = 0; d < 2; d++) {
                int c = e->coordinates[d * lanes + lane];
                if (c < realization[2 * d] || c >= realization[2 * d] + realization[2 * d + 1]) {
                    bad_loads++;
                }
            }
        }
    }
    return 0;
}

bool check(int mode) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), yo("yo"), yi("yi"), xoo("xoo"), xoi("xoi");
    Func g("g"), f("f"), out("out");
    bool update = mode == 3;
    if (update) {
        f(x, y) = 7;
        f(x, y) = f(x, y) + 1;
    } else {
        g(x, y) = x + 100 * y;
        f(x, y) = g(x, y) + 1;
        g.compute_root();
    }
    out(x, y) = f(x, y);
    f.compute_root().trace_loads().trace_realizations();
    traced = f.name();
    Stage s = update ? Stage(f.update()) : Stage(f);
    switch (mode) {
    case 0:
        s.split(x, xo, xi, 3, TailStrategy::PredicateStores)
            .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend)
            .reorder(xi, yi, xo, yo);
        break;
    case 1:
        s.split(x, xo, xi, 3, TailStrategy::PredicateStores)
            .split(xo, xoo, xoi, 2, TailStrategy::ShiftInwardsAndBlend);
        break;
    case 2:
        s.split(x, xo, xi, 8, TailStrategy::PredicateStores)
            .vectorize(xi)
            .split(y, yo, yi, 4, TailStrategy::ShiftInwardsAndBlend);
        break;
    case 3:
        s.split(x, xo, xi, 3, TailStrategy::PredicateStores)
            .split(xo, xoo, xoi, 2, TailStrategy::PredicateLoads)
            .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend)
            .reorder(xi, yi, xoi, xoo, yo);
        break;
    }
    Pipeline p(out);
    p.jit_handlers().custom_trace = trace_fn;
    for (int nx = 1; nx <= 17; nx++) {
        for (int ny = 1; ny <= 6; ny++) {
            Buffer<int> b(nx, ny);
            b.set_min(-1, 2);
            bad_loads = 0;
            p.realize(b);
            if (bad_loads) {
                printf("Mode %d, %dx%d: %d loads of f outside its realization [%d+%d]x[%d+%d]\n",
                       mode, nx, ny, bad_loads, realization[0], realization[1], realization[2],
                       realization[3]);
                return false;
            }
            for (int yv = 2; yv < 2 + ny; yv++) {
                for (int xv = -1; xv < nx - 1; xv++) {
                    int want = update ? 8 : xv + 100 * yv + 1;
                    if (b(xv, yv) != want) {
                        printf("Mode %d, %dx%d: out(%d, %d) = %d instead of %d\n", mode, nx, ny,
                               xv, yv, b(xv, yv), want);
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    for (int mode = 0; mode < 4; mode++) {
        if (!check(mode)) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
