#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x;

    f(x) = x;
    g(x) = f(x) + f(2 * x);

    f.compute_at(g, x).store_in(MemoryType::Stack);

    // We never free stack until function exit, so the schedule above
    // would seem to grab more and more stack because the allocation
    // size for f keeps growing. Fear not! After crossing a threshold
    // of total stack used per Func, we bail and start making heap
    // allocations instead.

    // The following would use 200 mb of stack if we just kept
    // reallocating:
    g.realize({10240});

    printf("Success!\n");
    return 0;
}
