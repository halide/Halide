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
        // Naming a dimension that a loop does correspond to is allowed, and
        // just pins sliding to that loop. It should be no worse than letting
        // sliding window pick for itself - same number of evaluations, and
        // the same folded allocation.
        Func f("f"), g("g");
        Var x;
        f(x) = counted(x);
        g(x) = f(x) + f(x - 1);
        f.store_root().compute_at(g, x).slide(g, x);

        AllocationSizeOf alloc(f.name());
        g.add_custom_lowering_pass(&alloc, nullptr);

        int n = evaluations(g, size);
        if (n != ideal) {
            printf("Naming a loop: f ran %d times, expected %d\n", n, ideal);
            return 1;
        }
        if (alloc.size != 2) {
            printf("Naming a loop gave f an allocation of %d, expected 2\n",
                   alloc.size);
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
        // and folding rounds that up to a power of two.
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

    {
        // The dimension can belong to an update stage, in which case it is an
        // RVar rather than a Var. Nothing else changes: split it, unroll the
        // inner loop, and name the RVar to slide along it.
        Func f("f"), g("g");
        Var x;
        RVar ro("ro"), ri("ri");
        f(x) = counted(x);
        g(x) = 0;
        RDom r(1, size - 1);
        g(r) = f(r) + f(r - 1);
        g.update().split(r, ro, ri, 2).unroll(ri);
        f.store_root().compute_at(g, ri).slide(g, r);

        AllocationSizeOf alloc(f.name());
        g.add_custom_lowering_pass(&alloc, nullptr);

        call_count = 0;
        Buffer<int> out = g.realize({size});
        for (int i = 1; i < size; i++) {
            int correct = i + (i - 1);
            if (out(i) != correct) {
                printf("g(%d) = %d instead of %d\n", i, out(i), correct);
                return 1;
            }
        }
        // One value of f per output, plus one to warm the window up.
        if (call_count != size) {
            printf("Sliding over an RVar: f ran %d times, expected %d\n",
                   call_count, size);
            return 1;
        }
        if (alloc.size != 2) {
            printf("Sliding over an RVar gave f an allocation of %d, "
                   "expected 2\n",
                   alloc.size);
            return 1;
        }
    }

    {
        // The region required can move with loops inside the dimension slid
        // over too. Here it moves with x as well as y, and f is computed per
        // value of x, but f's storage holds every x, so each x slides along y
        // with a window of its own.
        Func f("f"), g("g");
        Var x("x"), y("y"), yo("yo"), yi("yi");
        f(x, y) = counted(x + y);
        g(x, y) = f(x, x + y) + f(x, x + y - 1);
        g.split(y, yo, yi, 4, TailStrategy::RoundUp);
        f.store_root().compute_at(g, x).slide(g, y);

        call_count = 0;
        Buffer<int> out = g.realize({8, 16});
        for (int yy = 0; yy < 16; yy++) {
            for (int xx = 0; xx < 8; xx++) {
                int correct = (2 * xx + yy) + (2 * xx + yy - 1);
                if (out(xx, yy) != correct) {
                    printf("g(%d, %d) = %d instead of %d\n", xx, yy,
                           out(xx, yy), correct);
                    return 1;
                }
            }
        }
        // One value of f per output, plus one per column to warm up.
        if (call_count != 8 * 17) {
            printf("Sliding over a dimension the region also moves across: "
                   "f ran %d times, expected %d\n",
                   call_count, 8 * 17);
            return 1;
        }
    }

    {
        // Warming the window up prepends iterations to the loop outside the
        // dimension, and nothing else computed in that loop may run on them.
        // h reads exactly the input's extent, so running it early would read
        // out of bounds.
        Buffer<int> input(size);
        for (int i = 0; i < size; i++) {
            input(i) = i;
        }
        Func f("f"), g("g"), h("h");
        Var x("x"), xo("xo"), xi("xi");
        f(x) = counted(x);
        h(x) = input(x) - x;
        g(x) = f(x) + f(x - 1) + h(x);
        g.split(x, xo, xi, 2, TailStrategy::RoundUp);
        h.compute_at(g, xo);
        f.store_root().compute_at(g, xi).slide(g, x);

        int n = evaluations(g, size);
        if (n != ideal) {
            printf("Sliding over the dimension with h computed at xo: f ran "
                   "%d times, expected %d\n",
                   n, ideal);
            return 1;
        }
    }

    {
        // Something f consumes, computed outside the dimension, runs on the
        // warm-up iterations too, but only over what f's warm-up needs. The
        // input covers exactly what the algorithm reads, so anything more
        // would read out of bounds.
        Buffer<int> input(size + 1);
        input.set_min(-1);
        for (int i = -1; i < size; i++) {
            input(i) = i;
        }
        Func f("f"), g("g"), h("h");
        Var x("x"), xo("xo"), xi("xi");
        h(x) = input(x);
        f(x) = h(x);
        g(x) = f(x) + f(x - 1);
        g.split(x, xo, xi, 2, TailStrategy::RoundUp);
        h.compute_at(g, xo);
        f.store_root().compute_at(g, xi).slide(g, x);

        Buffer<int> out = g.realize({size});
        for (int i = 0; i < size; i++) {
            if (out(i) != 2 * i - 1) {
                printf("Sliding over the dimension with f's input computed "
                       "at xo: g(%d) = %d instead of %d\n",
                       i, out(i), 2 * i - 1);
                return 1;
            }
        }
    }

    {
        // An async producer runs ahead of its consumer, coupled by a
        // semaphore on the folded storage, so the fold has to know how far
        // it may run ahead as the dimension advances.
        Func f("f"), g("g");
        Var x("x"), xo("xo"), xi("xi");
        f(x) = x * x;
        g(x) = f(x) + f(x - 1);
        g.split(x, xo, xi, 2, TailStrategy::RoundUp).unroll(xi);
        f.store_root().compute_at(g, xi).slide(g, x).async();

        AllocationSizeOf alloc(f.name());
        g.add_custom_lowering_pass(&alloc, nullptr);
        Callable c = g.compile_to_callable({});
        const int n = 1024;
        Buffer<int> out(n);
        for (int trial = 0; trial < 100; trial++) {
            out.fill(0);
            if (c(out) != 0) {
                return 1;
            }
            for (int i = 0; i < n; i++) {
                int correct = i * i + (i - 1) * (i - 1);
                if (out(i) != correct) {
                    printf("Async slide: g(%d) = %d instead of %d\n", i, out(i), correct);
                    return 1;
                }
            }
        }
        if (alloc.size == 0 || alloc.size > 4) {
            printf("Sliding an async producer over the dimension gave f an "
                   "allocation of %d, expected a small constant\n",
                   alloc.size);
            return 1;
        }
    }

    {
        // Splitting a loop by one doesn't change the loops generated, so it
        // mustn't change how tightly f folds either, including when the index
        // reaching back carries a promise about staying in range.
        const int n = 16;
        for (bool promise : {false, true}) {
            for (int factor : {0, 1, 2}) {
                Func f("f"), g("g");
                Var x("x"), xo("xo"), xi("xi");
                f(x) = x * x;
                Expr back = x - 1;
                if (promise) {
                    back = unsafe_promise_clamped(back, 0, n - 1);
                }
                g(x) = f(x) + f(clamp(back, 0, n - 1));
                g.bound(x, 0, n);
                if (factor == 0) {
                    f.store_root().compute_at(g, x).slide(g, x);
                } else if (factor == 1) {
                    g.split(x, xo, xi, 1);
                    f.store_root().compute_at(g, xi).slide(g, x);
                } else {
                    g.align_bounds(x, factor).split(x, xo, xi, factor).unroll(xi);
                    f.store_root().compute_at(g, xi).slide(g, x);
                }

                AllocationSizeOf alloc(f.name());
                g.add_custom_lowering_pass(&alloc, nullptr);
                Buffer<int> out = g.realize({n});
                for (int i = 0; i < n; i++) {
                    int p = std::max(i - 1, 0);
                    if (out(i) != i * i + p * p) {
                        printf("g(%d) = %d instead of %d\n", i, out(i), i * i + p * p);
                        return 1;
                    }
                }
                if (alloc.size != 2) {
                    printf("f was allocated %d elements instead of 2, with x "
                           "split by %d and an index that %s a promise\n",
                           alloc.size, factor, promise ? "carries" : "does not carry");
                    return 1;
                }
            }
        }
    }

    {
        // The consumer reads f at reversed coordinates, so the window slides
        // down the split dimension. The select makes the region required jump
        // between the first two iterations and move by one after that.
        const int w = 8, h = 8;
        Var x("x"), y("y"), yo("yo"), yi("yi");
        Func in("in"), f("f"), g("g");
        in(x, y) = x * 3 + y;
        f(x, y) = in(x, y) + in(x + 1, y) + in(x, y + 1);
        Expr yy = select(y < 1, 0, 2) + y;
        g(x, y) = f(15 - x, 16 - yy) + f(16 - x, 16 - yy);
        in.compute_root();
        g.split(y, yo, yi, 4, TailStrategy::GuardWithIf);
        f.store_at(g, Var::outermost()).compute_at(g, x).slide(g, y);

        Buffer<int> out = g.realize({w, h});
        auto f_ref = [](int i, int j) {
            return (i * 3 + j) + (i * 3 + 3 + j) + (i * 3 + j + 1);
        };
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                int jj = (j < 1 ? 0 : 2) + j;
                int correct = f_ref(15 - i, 16 - jj) + f_ref(16 - i, 16 - jj);
                if (out(i, j) != correct) {
                    printf("Sliding down: g(%d, %d) = %d instead of %d\n",
                           i, j, out(i, j), correct);
                    return 1;
                }
            }
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
            g.split(y, yo, yi, 4, TailStrategy::RoundUp);
            // y is spread across yo and yi, but f's storage only lives for
            // one iteration of yo. Sliding over y would expect values from a
            // previous iteration of yo, and the allocation holding them has
            // been remade by then.
            f.store_at(g, yo).compute_at(g, x).slide(g, y);
            g.realize({8, 16});
        });

    failures += !expect_user_error(
        "computed_outside", "would never move", [] {
            Func f("f"), g("g");
            Var x("x"), y("y");
            f(x, y) = x + y;
            g(x, y) = f(x, y) + f(x - 1, y);
            // f is computed once per row, so there's nothing left to slide
            // over the columns.
            f.store_root().compute_at(g, y).slide(g, x);
            g.realize({8, 8});
        });

    failures += !expect_user_error(
        "non_constant_factor", "not a constant", [] {
            Param<int> n;
            n.set(4);
            Func f("f"), g("g");
            Var x("x"), xo("xo"), xi("xi");
            f(x) = x;
            g(x) = f(x) + f(x - 1);
            // Each iteration of xo moves the window along by n.
            g.split(x, xo, xi, n, TailStrategy::RoundUp);
            f.store_root().compute_at(g, xi).slide(g, x);
            g.realize({16});
        });

    failures += !expect_user_error(
        "non_monotonic_dimension", "xi breaks the sequence", [] {
            Func f("f"), g("g");
            Var x("x"), xo("xo"), xi("xi");
            f(x) = x;
            g(x) = f(x) + f(x - 1);
            // Reordering the split puts the small term on the outer loop, so
            // x runs 0, 8, 16, ... and then 1, 9, 17, ... A window can only
            // move forwards, so there is nothing sensible to slide here.
            g.split(x, xo, xi, 8, TailStrategy::RoundUp).reorder(xo, xi);
            f.store_root().compute_at(g, xo).slide(g, x);
            g.realize({32});
        });

    failures += !expect_user_error(
        "shift_inwards_moves_backwards", "one at a time", [] {
            Func f("f"), g("g");
            Var x("x"), xo("xo"), xi("xi");
            f(x) = x;
            g(x) = f(x) + f(x - 1);
            // ShiftInwards pulls the last iteration back so it fits, so with
            // an extent of 10 and a factor of 4, x runs 0-3, 4-7, then 6-9.
            // Aligning the bounds to a multiple of the factor makes this
            // legal, because then the tail never has to shift.
            g.split(x, xo, xi, 4, TailStrategy::ShiftInwards);
            f.store_root().compute_at(g, xi).slide(g, x);
            g.realize({10});
        });

    failures += !expect_user_error(
        "interleaved_loops", "yi breaks the sequence", [] {
            Func f("f"), g("g");
            Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");
            f(x, y) = x + y;
            g(x, y) = f(x, y) + f(x - 1, y);
            // Tiling puts yi between xo and xi, so x runs through each tile's
            // four columns once per row of the tile, going backwards each
            // time yi advances.
            g.tile(x, y, xo, yo, xi, yi, 4, 4, TailStrategy::RoundUp);
            f.store_root().compute_at(g, xi).slide(g, x);
            g.realize({16, 16});
        });

    failures += !expect_user_error(
        "parallel_dimension", "one iteration at a time", [] {
            Func f("f"), g("g");
            Var x("x");
            f(x) = x;
            g(x) = f(x) + f(x - 1);
            // Each iteration of a parallel loop may run before the one
            // before it, so there is no previous iteration to inherit from.
            g.parallel(x);
            f.store_root().compute_at(g, x).slide(g, x);
            g.realize({16});
        });

    failures += !expect_user_error(
        "entangled_dimensions", "move the same dimension", [] {
            Func f("f"), g("g");
            Var x("x"), y("y"), xo("xo"), xi("xi");
            f(x) = x;
            g(x, y) = f(x + y) + f(x + y - 1);
            g.split(x, xo, xi, 4, TailStrategy::RoundUp);
            // Sliding over several dimensions at once is fine, but these two
            // aren't independent - both of them move the window along f's
            // only dimension, and it can only advance along it once.
            f.store_root().compute_at(g, xi).slide(g, x).slide(g, y);
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
