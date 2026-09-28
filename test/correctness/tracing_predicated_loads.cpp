#include "Halide.h"
#include <cstdio>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace Halide;

// A PredicateLoads split masks the loads of the tail iterations. When the
// loop is vectorized, the masked load becomes a predicated vector load. With
// trace_loads, the load is wrapped in a trace call, and it used to lose its
// predicate: the traced program read the masked-off lanes, past the end of
// the producer's allocation.

#ifndef _WIN32
namespace {

// Place each allocation so that its (64-byte rounded) end is followed by an
// inaccessible page. Allocations are never freed.
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

int ignore_trace(JITUserContext *, const halide_trace_event_t *) {
    return 0;
}

bool check(bool trace, int vec, int n) {
    Var x("x"), xo("xo"), xi("xi");
    Func f("f"), g("g"), h("h");
    f(x) = x;
    g(x) = f(x) * 2;
    h(x) = g(x);
    f.compute_root();
    if (trace) {
        f.trace_loads();
    }
    g.compute_root().split(x, xo, xi, vec, TailStrategy::PredicateLoads).vectorize(xi);
    h.jit_handlers().custom_malloc = guarded_malloc;
    h.jit_handlers().custom_free = guarded_free;
    h.jit_handlers().custom_trace = ignore_trace;
    Buffer<int> out = h.realize({n});
    for (int i = 0; i < n; i++) {
        if (out(i) != 2 * i) {
            printf("trace = %d, vec = %d, n = %d: out(%d) = %d instead of %d\n",
                   trace, vec, n, i, out(i), 2 * i);
            return false;
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
    // f's allocation is n ints; the last vector of g reads up to vec ints
    // from the last tile's base, so the masked-off lanes lie past the end.
    for (bool trace : {false, true}) {
        for (int vec : {32, 64}) {
            for (int n : {16, 48}) {
                if (!check(trace, vec, n)) {
                    return 1;
                }
            }
        }
    }
    printf("Success!\n");
    return 0;
#endif
}
