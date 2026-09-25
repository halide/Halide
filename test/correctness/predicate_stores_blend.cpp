#include "Halide.h"
#include <cstdio>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace Halide;

// A blend (RoundUpAndBlend / ShiftInwardsAndBlend) loads the value its store
// would overwrite. If an earlier PredicateStores split guarded the store's
// coordinates, bounds inference trusts that guard for the load too and sizes
// the allocation to the realized region, but the load used to run (and read
// out of bounds) where the store was predicated off.

#ifndef _WIN32
namespace {

// Scalar code: check traced loads of f against its realization. (Codegen
// evaluates a predicated store's value before the branch on the predicate,
// but LLVM is free to sink the load into the branch, so the out-of-bounds
// access may not happen in the generated code.)
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
        for (int d = 0; d < 2; d++) {
            int c = e->coordinates[d];
            if (c < realization[2 * d] || c >= realization[2 * d] + realization[2 * d + 1]) {
                bad_loads++;
            }
        }
    }
    return 0;
}

// Vectorized code: a trace of a predicated vector load reports all of its
// lanes, so instead place each allocation so that its (64-byte rounded) end
// is followed by an inaccessible page. Allocations are never freed.
void *guarded_malloc(JITUserContext *, size_t size) {
    size_t page = getpagesize();
    size_t n = (size + page - 1) / page * page;
    char *base = (char *)mmap(nullptr, n + page, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED || mprotect(base + n, page, PROT_NONE) != 0) {
        return nullptr;
    }
    return base + n - (size + 63) / 64 * 64;
}

void guarded_free(JITUserContext *, void *) {
}

bool check(int mode) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), yo("yo"), yi("yi");
    Var xoo("xoo"), xoi("xoi"), yoo("yoo"), yoi("yoi");
    Func g("g"), f("f"), out("out");
    bool update = mode == 3 || mode == 5;
    if (update) {
        f(x, y) = 7;
        f(x, y) = f(x, y) + 1;
    } else {
        g(x, y) = x + 100 * y;
        f(x, y) = g(x, y) + 1;
        g.compute_root();
    }
    out(x, y) = f(x, y);
    f.compute_root();
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
        // The blend's load of f(x, y) and the update's own load of it (masked
        // by the PredicateLoads split) are the same load under different
        // conditions.
        s.split(x, xo, xi, 3, TailStrategy::PredicateStores)
            .split(xo, xoo, xoi, 2, TailStrategy::PredicateLoads)
            .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend)
            .reorder(xi, yi, xoi, xoo, yo);
        break;
    case 4:
        s.split(x, xo, xi, 8, TailStrategy::PredicateStores)
            .vectorize(xi)
            .split(xo, xoo, xoi, 2, TailStrategy::ShiftInwardsAndBlend);
        break;
    case 5:
        // As 3, with both loads also masked by a PredicateLoads split on y.
        s.split(x, xo, xi, 3, TailStrategy::PredicateStores)
            .split(xo, xoo, xoi, 2, TailStrategy::PredicateLoads)
            .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend)
            .split(yo, yoo, yoi, 2, TailStrategy::PredicateLoads)
            .reorder(xi, yi, xoi, xoo, yoi, yoo);
        break;
    }
    bool vectorized = mode == 2 || mode == 4;
    if (!vectorized) {
        f.trace_loads().trace_realizations();
        traced = f.name();
    }
    Pipeline p(out);
    p.jit_handlers().custom_trace = trace_fn;
    p.jit_handlers().custom_malloc = guarded_malloc;
    p.jit_handlers().custom_free = guarded_free;
    for (int nx = 1; nx <= 17; nx++) {
        for (int ny = 1; ny <= 8; ny++) {
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
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    printf("[SKIP] Test requires mmap.\n");
    return 0;
#else
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Test requires custom host allocations.\n");
        return 0;
    }
    for (int mode = 0; mode < 6; mode++) {
        if (!check(mode)) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
#endif
}
