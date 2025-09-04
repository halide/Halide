#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

#if defined(_MSC_VER)
#include <intrin.h>
#include <malloc.h>
#define ALLOCA(size) _alloca(size)
#else
#define ALLOCA(size) __builtin_alloca(size)
#endif

const int N = 1024 * 1024 * 6;

void paint_stack() {
    constexpr int padding = 1024 * 1024;
    char *ptr = (char *)ALLOCA(N + padding);
    memset(ptr, 0x42, N + padding);
}

void check_stack() {
    char *ptr = (char *)ALLOCA(N);
#if defined(_MSC_VER)
    _ReadBarrier();
#else
    asm volatile("" : : "r"(ptr) : "memory");
#endif
    int i = 0;
    while (ptr[i] == 0x42) {
        i++;
    }
    printf("Peak stack usage = %d kb\n", (N - i) / 1024);
}

int main(int argc, char **argv) {

    paint_stack();

    // There were scalability problems with taking bounds of nested pure
    // intrinsics. This test hangs if those problems still exist, using the
    // strict float intrinsics. https://github.com/halide/Halide/issues/8686

    Param<float> p1, p2, p2_min, p2_max;
    Scope<Interval> scope;
    scope.push(p2.name(), Interval{p2_min, p2_max});

    // This test uses a lot of stack space, especially on ASAN, where we don't
    // do any stack switching (see Util.cpp). Don't push this number too far.
    for (int limit = 1; limit < 100; limit++) {
        Expr e1 = p1, e2 = p2;
        for (int i = 0; i < limit; i++) {
            e1 = e1 * p1 + (i + 1);
            e2 = e2 * p2 + (i + 1);
        }
        Expr e = e1 + e2;
        bounds_of_expr_in_scope(e, scope);
        e = strictify_float(e);
        bounds_of_expr_in_scope(e, scope);
    }

    // printf("Success!\n");

    check_stack();

    return -1;
}
