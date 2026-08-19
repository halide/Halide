#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Func::slide names the dimension a window slides along, instead of letting
// sliding window analysis pick a loop. This file is a set of worked examples
// of when you would want that.

// A lowering pass that counts the moduli in the indices where the consumer
// loads from a producer. Sliding window folds the producer's storage down to a
// couple of elements, and indexing into it costs a modulus unless the index is
// known at compile time. That's the cost the examples below are chasing, and
// it isn't visible in the output values, so we go and look for it in the IR.
class CountModsInLoadsFrom : public Internal::IRMutator {
    const std::string producer;

    using IRMutator::visit;

    Expr visit(const Internal::Load *op) override {
        if (op->name == producer) {
            Internal::visit_with(op->index,
                                 [&](auto *self, const Internal::Mod *op) {
                                     count++;
                                     self->visit_base(op);
                                 });
        }
        return IRMutator::visit(op);
    }

public:
    int count = 0;

    CountModsInLoadsFrom(std::string producer)
        : producer(std::move(producer)) {
    }
};

int call_count = 0;
extern "C" HALIDE_EXPORT_SYMBOL int counted(int x) {
    call_count++;
    return x;
}
HalideExtern_1(int, counted, int);

// Realize g and report how many times its producer ran.
int evaluations(Func g, int extent) {
    call_count = 0;
    Buffer<int> out = g.realize({extent});
    for (int i = 0; i < extent; i++) {
        int correct = i + (i - 1);
        if (out(i) != correct) {
            printf("g(%d) = %d instead of %d\n", i, out(i), correct);
            return -1;
        }
    }
    return call_count;
}

int main(int argc, char **argv) {
    const int size = 64;

    // A two-tap stencil. Sliding should let f be computed once per output,
    // plus one to warm the window up.
    const int ideal = size + 1;

    {
        // Sliding window normally slides over a loop. Here the loop it picks
        // is g's x, and each iteration computes just the one new value of f.
        // Storage folds to two elements, which costs a modulus on every
        // access, because x is not known at compile time.
        Func f("f"), g("g");
        Var x;
        f(x) = counted(x);
        g(x) = f(x) + f(x - 1);
        f.store_root().compute_at(g, x);

        CountModsInLoadsFrom mods(f.name());
        g.add_custom_lowering_pass(&mods, nullptr);

        int n = evaluations(g, size);
        if (n != ideal) {
            printf("Sliding over a loop: f ran %d times, expected %d\n", n, ideal);
            return 1;
        }
        if (mods.count == 0) {
            printf("Sliding over a loop did not cost a modulus, so this example "
                   "no longer motivates anything\n");
            return 1;
        }
    }

    {
        // The natural way to remove that modulus is to unroll by the fold
        // factor, so that each unrolled body knows its own index. But
        // splitting x means there is no longer a loop called x for the window
        // to slide along - there is an xo loop and an xi loop, and sliding
        // picks one of them. Sliding over xo advances the window two elements
        // at a time, so it computes more than it needs to.
        Func f("f"), g("g");
        Var x, xo, xi;
        f(x) = counted(x);
        g(x) = f(x) + f(x - 1);
        g.align_bounds(x, 2).split(x, xo, xi, 2).unroll(xi);
        f.store_root().compute_at(g, xi);

        int n = evaluations(g, size);
        if (n < 0) {
            return 1;
        }
        if (n <= ideal) {
            printf("Sliding over a split loop did not overcompute (%d), so this "
                   "example no longer demonstrates anything\n",
                   n);
            return 1;
        }
    }

    {
        // Naming the dimension gets the best of both. x still exists after the
        // split - it just isn't a loop any more, it's a value computed from xo
        // and xi - and the window can slide along it. Each unrolled body then
        // knows its index into the folded buffer, so the modulus goes away.
        Func f("f"), g("g");
        Var x, xo, xi;
        f(x) = counted(x);
        g(x) = f(x) + f(x - 1);
        g.align_bounds(x, 2).split(x, xo, xi, 2).unroll(xi);
        f.store_root().compute_at(g, xi).slide(g, x);

        CountModsInLoadsFrom mods(f.name());
        g.add_custom_lowering_pass(&mods, nullptr);

        int n = evaluations(g, size);
        if (n != ideal) {
            printf("Sliding over the dimension: f ran %d times, expected %d\n", n, ideal);
            return 1;
        }
        if (mods.count != 0) {
            printf("Sliding over the dimension left %d moduli in the loads from "
                   "f, expected none\n",
                   mods.count);
            return 1;
        }
    }

    {
        // The dimension named doesn't have to be one of the Func's original
        // Vars. Split x twice and the loops are xoo, xoi and xi, while xo is
        // neither an original Var nor a loop. It's still a dimension the
        // window can slide along. Compute f at xoi, which is where xo changes.
        Func f("f"), g("g");
        Var x, xo, xi, xoo, xoi;
        f(x) = counted(x);
        g(x) = f(x) + f(x - 4);
        g.align_bounds(x, 8).split(x, xo, xi, 4).split(xo, xoo, xoi, 2);
        f.store_root().compute_at(g, xoi).slide(g, xo);

        call_count = 0;
        Buffer<int> out = g.realize({size});
        for (int i = 0; i < size; i++) {
            int correct = i + (i - 4);
            if (out(i) != correct) {
                printf("g(%d) = %d instead of %d\n", i, out(i), correct);
                return 1;
            }
        }
        // Without the directive f runs twice per output, because the window
        // can only slide along one of the loops.
        if (call_count >= 2 * size) {
            printf("Sliding over an intermediate Var: f ran %d times, which is "
                   "no better than not sliding\n",
                   call_count);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
