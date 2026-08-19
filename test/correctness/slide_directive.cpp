#include "Halide.h"
#include "expect_user_error.h"
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

// A lowering pass that reports the size of a producer's allocation, or 0 if
// it doesn't have a constant size. Sliding is what makes the allocation small,
// and storage folding is what makes it constant.
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
        AllocationSizeOf alloc(f.name());
        g.add_custom_lowering_pass(&alloc, nullptr);

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
        // Two taps, so the window is two elements wide.
        if (alloc.size != 2) {
            printf("Sliding over the dimension gave f an allocation of %d, "
                   "expected 2\n",
                   alloc.size);
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

        AllocationSizeOf alloc(f.name());
        g.add_custom_lowering_pass(&alloc, nullptr);

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
        // The window spans xi as well as one step of xo, so it is five wide,
        // and folding rounds that up to a power of two. Sliding window works
        // this out and leaves it behind for storage folding, which can't see
        // the dimension any more by the time it runs.
        if (alloc.size != 8) {
            printf("Sliding over an intermediate Var gave f an allocation of "
                   "%d, expected 8\n",
                   alloc.size);
            return 1;
        }
    }

    {
        // A window can slide over more than one dimension at once, which
        // sliding window analysis already does when it picks loops for
        // itself. Naming dimensions doesn't give that up - the calls
        // accumulate, and each named dimension advances the window along a
        // different dimension of the producer.
        Func f("f"), g("g");
        Var x, y;
        f(x, y) = counted(x + y);
        g(x, y) = f(x, y) + f(x - 1, y) + f(x, y - 1) + f(x - 1, y - 1);
        f.store_root().compute_at(g, x).slide(g, x).slide(g, y);

        call_count = 0;
        Buffer<int> out = g.realize({size, size});
        for (int yy = 0; yy < size; yy++) {
            for (int xx = 0; xx < size; xx++) {
                int correct = (xx + yy) + (xx - 1 + yy) +
                              (xx + yy - 1) + (xx - 1 + yy - 1);
                if (out(xx, yy) != correct) {
                    printf("g(%d, %d) = %d instead of %d\n", xx, yy,
                           out(xx, yy), correct);
                    return 1;
                }
            }
        }
        // One value of f per output, plus a row and a column to warm up.
        const int ideal_2d = (size + 1) * (size + 1);
        if (call_count != ideal_2d) {
            printf("Sliding over two dimensions: f ran %d times, expected %d\n",
                   call_count, ideal_2d);
            return 1;
        }
    }

    // The rest of this file is the scheduling mistakes the directive rejects.
    // Each of them would otherwise slide a window over values that aren't
    // there any more, or quietly not slide at all.
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        printf("[SKIP] the error cases need exceptions.\n");
        printf("Success!\n");
        return 0;
    }

    int failures = 0;

    failures += !expect_user_error(
        "storage_too_narrow", "have been thrown away", [] {
            Func f("f"), g("g");
            Var x("x"), y("y"), yo("yo"), yi("yi");
            f(x, y) = x + y;
            g(x, y) = f(x, y) + f(x, y - 1);
            g.split(y, yo, yi, 4);
            // y is spread across yo and yi, but f's storage only lives for
            // one iteration of yo. Sliding over y would expect values from a
            // previous iteration of yo, and the allocation holding them has
            // been remade by then.
            f.store_at(g, yo).compute_at(g, x).slide(g, y);
            g.realize({8, 16});
        });

    failures += !expect_user_error(
        "region_moves_with_inner_loop", "within a single value", [] {
            Func f("f"), g("g");
            Var x("x"), y("y"), yo("yo"), yi("yi");
            f(x, y) = x + y;
            // A shear: the region of f required moves with x as well as y.
            g(x, y) = f(x, x + y) + f(x, x + y - 1);
            g.split(y, yo, yi, 4);
            // Sliding over y advances the window once per value of y, but x
            // moves the region required too, so the window would have to
            // advance within a single value of y as well.
            f.store_root().compute_at(g, x).slide(g, y);
            g.realize({8, 16});
        });

    failures += !expect_user_error(
        "entangled_dimensions", "move the same dimension", [] {
            Func f("f"), g("g");
            Var x("x"), y("y"), xo("xo"), xi("xi");
            f(x, y) = x + y;
            g(x, y) = f(x, y) + f(x - 1, y);
            g.split(x, xo, xi, 4);
            // Sliding over several dimensions at once is fine, but these two
            // aren't independent - xo is a split of x, so both of them move
            // the window along f's first dimension, and it can only advance
            // along that dimension once.
            f.store_root().compute_at(g, xi).slide(g, x).slide(g, xo);
            g.realize({16, 16});
        });

    if (failures != 0) {
        printf("%d bad schedule(s) were not rejected\n", failures);
        return 1;
    }
#endif

    printf("Success!\n");
    return 0;
}
