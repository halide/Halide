#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// The third argument to Func::slide computes each sliver some number of
// iterations before the one that consumes it, so that a producer with a long
// latency has that many iterations to finish. The storage grows to hold what
// is in flight.

// Reports the size of a producer's allocation, or 0 if it isn't constant.
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

int main(int argc, char **argv) {
    const int size = 32;

    // A window two elements wide. Without pipelining it needs two elements of
    // storage; each extra iteration in flight needs one more.
    for (int depth = 0; depth <= 3; depth++) {
        Func f("f"), g("g");
        Var x;
        f(x) = x * x;
        g(x) = f(x) + f(x - 1);
        f.store_root().compute_at(g, x).slide(g, x, depth);

        AllocationSizeOf alloc(f.name());
        g.add_custom_lowering_pass(&alloc, nullptr);

        Buffer<int> out = g.realize({size});
        for (int i = 0; i < size; i++) {
            int correct = i * i + (i - 1) * (i - 1);
            if (out(i) != correct) {
                printf("depth %d: g(%d) = %d instead of %d\n", depth, i,
                       out(i), correct);
                return 1;
            }
        }

        // Two for the window, plus one per iteration in flight, rounded up to
        // a power of two by folding.
        int wanted = 1;
        while (wanted < depth + 2) {
            wanted *= 2;
        }
        if (alloc.size != wanted) {
            printf("depth %d: f's allocation is %d, expected %d\n", depth,
                   alloc.size, wanted);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
