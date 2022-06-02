#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>
#include <memory>

using namespace Halide;
using namespace Halide::Tools;

int call_count = 0;
extern "C" HALIDE_EXPORT_SYMBOL int call_counter(int x, int y) {
    call_count++;
    return x;
}
HalideExtern_2(int, call_counter, int, int);

int main(int argc, char **argv) {
    Var x, y;

    // A test case that requires sliding window to be able to slide
    // over a guardwithif split + promise_clamped.

    Func expensive;
    expensive(x, y) = call_counter(x, y);

    Func dst;
    dst(x, y) = expensive(x, y - 1) + expensive(x, y) + expensive(x, y + 1);

    Var yo("yo");
    dst.compute_root()
        .split(y, yo, y, 64, TailStrategy::GuardWithIf);

    expensive
        .compute_at(dst, y)
        .store_at(dst, yo)
        .fold_storage(y, 4);

    Buffer<int> out = dst.realize({100, 100});

    // The number of calls to 'expensive' should be the size of the
    // output, plus some margin from the stencil, plus a little
    // redundant recompute at the split boundary.
    int correct = (out.height() + 2 + 2) * out.width();
    if (call_count != correct) {
        printf("number of calls to producer was %d instead of %d\n",
               call_count, correct);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
