#include "Halide.h"

using namespace Halide;

// This is a test for a bug where the condition on a realize node didn't have
// tuple-valued calls resolved if the realization was itself tuple-valued.

int main(int argc, char **argv) {
    Func f;
    Param<int> p;
    f() = {p, p};

    Func g;
    g() = {4, 4};

    Func h;
    h() = g()[1];

    // h may or may not be necessary to evaluate, depending on a load from f,
    // which means g in turn may or may not be necessary to allocate.
    Func out;
    out() = select(f()[1] == 3, h(), 17);

    f.compute_root();
    g.compute_root();
    h.compute_root();
    out.compile_jit();

    printf("Success!\n");
    return 0;
}
