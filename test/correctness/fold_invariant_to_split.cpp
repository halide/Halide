#include "Halide.h"
#include <sstream>
#include <stdio.h>
#include <string>

using namespace Halide;

// Splitting a loop by one doesn't change the loops that get generated, so it
// must not change how tightly the sliding window's storage folds. It used to:
// a dimension that survived as a loop was measured by storage folding, while
// one spread across a split was described to it by sliding window, and the
// two didn't agree on indices carrying promises about staying in range.

namespace {

// The size of a producer's allocation.
class AllocationSizeOf : public Internal::IRMutator {
    const std::string producer;
    using IRMutator::visit;
    Internal::Stmt visit(const Internal::Allocate *op) override {
        if (op->name == producer) {
            int64_t total = 1;
            for (const auto &e : op->extents) {
                total *= Internal::as_const_int(e).value_or(0);
            }
            size = (int)total;
        }
        return IRMutator::visit(op);
    }

public:
    int size = 0;
    AllocationSizeOf(std::string producer)
        : producer(std::move(producer)) {
    }
};

const int N = 16;

// How the consumer's loop over x is scheduled. The producer slides along x
// either way, and two values of it are live at a time in every case.
enum Schedule { Unsplit,
                SplitByOne,
                SplitByTwo };

// promise: reach back through an index that promises it stays in range, which
// is what a stencil reading a bounded input looks like once the promise has
// been introduced.
int fold_factor_of(Schedule schedule, bool promise) {
    Func f("f"), g("g");
    Var x("x"), xo("xo"), xi("xi");

    f(x) = x * x;
    Expr back = x - 1;
    if (promise) {
        back = unsafe_promise_clamped(back, 0, N - 1);
    }
    g(x) = f(x) + f(clamp(back, 0, N - 1));
    g.bound(x, 0, N);

    switch (schedule) {
    case Unsplit:
        f.store_root().compute_at(g, x).slide(g, x);
        break;
    case SplitByOne:
        g.split(x, xo, xi, 1);
        f.store_root().compute_at(g, xi).slide(g, x);
        break;
    case SplitByTwo:
        g.align_bounds(x, 2).split(x, xo, xi, 2).unroll(xi);
        f.store_root().compute_at(g, xi).slide(g, x);
        break;
    }

    AllocationSizeOf alloc(f.name());
    g.add_custom_lowering_pass(&alloc, nullptr);
    Buffer<int> out = g.realize({N});

    for (int i = 0; i < N; i++) {
        int p = std::max(i - 1, 0);
        int correct = i * i + p * p;
        if (out(i) != correct) {
            printf("out(%d) = %d instead of %d\n", i, out(i), correct);
            return -1;
        }
    }
    return alloc.size;
}

}  // namespace

int main(int argc, char **argv) {
    const char *names[] = {"unsplit", "split by one", "split by two"};
    for (bool promise : {false, true}) {
        for (Schedule s : {Unsplit, SplitByOne, SplitByTwo}) {
            int size = fold_factor_of(s, promise);
            if (size < 0) {
                return 1;
            }
            // Two values of f are live at a time however x is scheduled.
            if (size != 2) {
                printf("f was allocated %d elements instead of 2, "
                       "with x %s and an index that %s a promise\n",
                       size, names[(int)s], promise ? "carries" : "does not carry");
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
