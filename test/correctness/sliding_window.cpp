#include "Halide.h"
#include <algorithm>
#include <stdio.h>

using namespace Halide;

int count = 0;
extern "C" HALIDE_EXPORT_SYMBOL int call_counter(int x, int y) {
    count++;
    return 0;
}
HalideExtern_2(int, call_counter, int, int);

extern "C" void *my_malloc(JITUserContext *, size_t x) {
    printf("Malloc wasn't supposed to be called!\n");
    exit(1);
}

int main(int argc, char **argv) {
    Var x, y;

    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    for (auto store_in : {MemoryType::Heap, MemoryType::Register}) {
        count = 0;
        Func f, g;

        f(x) = call_counter(x, 0);
        g(x) = f(x) + f(x - 1);

        f.store_root().compute_at(g, x).store_in(store_in);

        // Test that sliding window works when specializing.
        g.specialize(g.output_buffer().dim(0).min() == 0);

        Buffer<int> im = g.realize({100});

        // f should be able to tell that it only needs to compute each value once
        if (count != 101) {
            printf("f was called %d times instead of %d times\n", count, 101);
            return 1;
        }
    }

    // Try two producers used by the same consumer.
    for (auto store_in : {MemoryType::Heap, MemoryType::Register}) {
        count = 0;
        Func f, g, h;

        f(x) = call_counter(2 * x + 0, 0);
        g(x) = call_counter(2 * x + 1, 0);
        h(x) = f(x) + f(x - 1) + g(x) + g(x - 1);

        f.store_root().compute_at(h, x).store_in(store_in);
        g.store_root().compute_at(h, x).store_in(store_in);

        Buffer<int> im = h.realize({100});
        if (count != 202) {
            printf("f was called %d times instead of %d times\n", count, 202);
            return 1;
        }
    }

    // Try a sequence of two sliding windows.
    for (auto store_in : {MemoryType::Heap, MemoryType::Register}) {
        count = 0;
        Func f, g, h;

        f(x) = call_counter(2 * x + 0, 0);
        g(x) = f(x) + f(x - 1);
        h(x) = g(x) + g(x - 1);

        f.store_root().compute_at(h, x).store_in(store_in);
        g.store_root().compute_at(h, x).store_in(store_in);

        Buffer<int> im = h.realize({100});
        int correct = store_in == MemoryType::Register ? 103 : 102;
        if (count != correct) {
            printf("f was called %d times instead of %d times\n", count, correct);
            return 1;
        }
    }

    // Try again where there's a containing stage
    for (auto store_in : {MemoryType::Heap, MemoryType::Register}) {
        count = 0;
        Func f, g, h;
        f(x) = call_counter(x, 0);
        g(x) = f(x) + f(x - 1);
        h(x) = g(x);

        f.store_root().compute_at(g, x).store_in(store_in);
        g.compute_at(h, x);

        Buffer<int> im = h.realize({100});
        if (count != 101) {
            printf("f was called %d times instead of %d times\n", count, 101);
            return 1;
        }
    }

    // Add an inner vectorized dimension.
    for (auto store_in : {MemoryType::Heap, MemoryType::Register}) {
        count = 0;
        Func f, g, h;
        Var c;
        f(x, c) = call_counter(x, c);
        g(x, c) = f(x + 1, c) - f(x, c);
        h(x, c) = g(x, c);

        f.store_root()
            .compute_at(h, x)
            .store_in(store_in)
            .reorder(c, x)
            .reorder_storage(c, x)
            .bound(c, 0, 4)
            .vectorize(c);

        g.compute_at(h, x);

        h.reorder(c, x).reorder_storage(c, x).bound(c, 0, 4).vectorize(c);

        Buffer<int> im = h.realize({100, 4});
        if (count != 404) {
            printf("f was called %d times instead of %d times\n", count, 404);
            return 1;
        }
    }

    // Now try with a reduction
    {
        count = 0;
        RDom r(0, 100);
        Func f, g;

        f(x, y) = 0;
        f(r, y) = call_counter(r, y);
        f.store_root().compute_at(g, y);

        g(x, y) = f(x, y) + f(x, y - 1);

        Buffer<int> im = g.realize({10, 10});

        // For each value of y, f should be evaluated over (0 .. 100) in
        // x, and (y .. y-1) in y. Sliding window optimization means that
        // we can skip the y-1 case in all but the first iteration.
        if (count != 100 * 11) {
            printf("f was called %d times instead of %d times\n", count, 100 * 11);
            return 1;
        }
    }

    {
        // Now try sliding over multiple dimensions at once
        Func f, g;

        count = 0;
        f(x, y) = call_counter(x, y);
        g(x, y) = f(x - 1, y) + f(x, y) + f(x, y - 1);
        f.store_root().compute_at(g, x);

        Buffer<int> im = g.realize({10, 10});

        if (count != 11 * 11) {
            printf("f was called %d times instead of %d times\n", count, 11 * 11);
            return 1;
        }
    }

    {
        Func f, g;

        // Now a trickier example. In order for this to work, Halide would have to slide diagonally. We don't handle this.
        count = 0;
        f(x, y) = call_counter(x, y);
        // When x was two smaller the second term was computed. When y was two smaller the third term was computed.
        g(x, y) = f(x + y, x - y) + f((x - 2) + y, (x - 2) - y) + f(x + (y - 2), x - (y - 2));
        f.store_root().compute_at(g, x);

        Buffer<int> im = g.realize({10, 10});
        if (count != 1500) {
            printf("f was called %d times instead of %d times\n", count, 1500);
            return 1;
        }
    }

    {
        // Now make sure Halide folds the example in Func.h down to a stack allocation
        Func f, g;
        f(x, y) = x * y;
        g(x, y) = f(x, y) + f(x + 1, y) + f(x, y + 1) + f(x + 1, y + 1);
        f.store_at(g, y).compute_at(g, x);
        g.jit_handlers().custom_malloc = my_malloc;
        Buffer<int> im = g.realize({10, 10});
    }

    {
        // Sliding where the footprint is actually fixed over the loop
        // var. Everything in the producer should be computed in the
        // first iteration.
        Func f, g;

        f(x) = call_counter(x, 0);
        g(x) = f(0) + f(5);

        f.store_root().compute_at(g, x);

        count = 0;
        Buffer<int> im = g.realize({100});

        // f should be able to tell that it only needs to compute each value once
        if (count != 6) {
            printf("f was called %d times instead of %d times\n", count, 6);
            return 1;
        }
    }

    for (auto store_in : {MemoryType::Heap, MemoryType::Register}) {
        // Sliding where we only need a new value every third iteration of the consumer.
        Func f, g;

        f(x) = call_counter(x, 0);
        g(x) = f(x / 3);

        f.store_root().compute_at(g, x).store_in(store_in);

        count = 0;
        Buffer<int> im = g.realize({100});

        // f should be able to tell that it only needs to compute each value once
        if (count != 34) {
            printf("f was called %d times instead of %d times\n", count, 34);
            return 1;
        }
    }

    for (auto store_in : {MemoryType::Heap, MemoryType::Register}) {
        // Sliding where we only need a new value every third iteration of the consumer.
        // This test checks that we don't ask for excessive bounds.
        ImageParam f(Int(32), 1);
        Func g;

        g(x) = f(x / 3);

        Var xo;
        g.split(x, xo, x, 10);
        f.in().store_at(g, xo).compute_at(g, x).store_in(store_in);

        Buffer<int> buf(33);
        f.set(buf);

        Buffer<int> im = g.realize({98});
    }

    for (auto store_in : {MemoryType::Heap, MemoryType::Register}) {
        // Sliding with an unrolled producer
        Var x, xi;
        Func f, g;

        f(x) = call_counter(x, 0) + x * x;
        g(x) = f(x) + f(x - 1);

        g.split(x, x, xi, 10);
        f.store_root().compute_at(g, x).store_in(store_in).unroll(x);

        count = 0;
        Buffer<int> im = g.realize({100});

        if (count != 101) {
            printf("f was called %d times instead of %d times\n", count, 101);
            return 1;
        }
    }

    {
        // Sliding with a vectorized producer and consumer.
        count = 0;
        Func f, g;
        f(x) = call_counter(x, 0);
        g(x) = f(x + 1) + f(x - 1);

        f.store_root().compute_at(g, x).vectorize(x, 4);
        g.vectorize(x, 4);

        Buffer<int> im = g.realize({100});
        if (count != 104) {
            printf("f was called %d times instead of %d times\n", count, 104);
            return 1;
        }
    }

    {
        // Sliding with a vectorized producer and consumer, trying to rotate
        // cleanly in registers.
        count = 0;
        Func f, g;
        f(x) = call_counter(x, 0);
        g(x) = f(x + 1) + f(x - 1);

        // This currently requires a trick to get everything to be aligned
        // nicely. This exploits the fact that ShiftInwards splits are
        // aligned to the end of the original loop (and extending before the
        // min if necessary).
        Var xi("xi");
        f.store_root().compute_at(g, x).store_in(MemoryType::Register).split(x, x, xi, 8).vectorize(xi, 4).unroll(xi);
        g.vectorize(x, 4, TailStrategy::RoundUp);

        Buffer<int> im = g.realize({100});
        if (count != 102) {
            printf("f was called %d times instead of %d times\n", count, 102);
            return 1;
        }
    }

    {
        // A sequence of stencils, all computed at the output.
        count = 0;
        Func f, g, h, u, v;
        f(x, y) = call_counter(x, y);
        g(x, y) = f(x, y - 1) + f(x, y + 1);
        h(x, y) = g(x - 1, y) + g(x + 1, y);
        u(x, y) = h(x, y - 1) + h(x, y + 1);
        v(x, y) = u(x - 1, y) + u(x + 1, y);

        u.compute_at(v, y);
        h.store_root().compute_at(v, y);
        g.store_root().compute_at(v, y);
        f.store_root().compute_at(v, y);

        v.realize({10, 10});
        if (count != 14 * 14) {
            printf("f was called %d times instead of %d times\n", count, 14 * 14);
            return 1;
        }
    }

    {
        // A sequence of stencils, sliding computed at the output.
        count = 0;
        Func f, g, h, u, v;
        f(x, y) = call_counter(x, y);
        g(x, y) = f(x, y - 1) + f(x, y + 1);
        h(x, y) = g(x - 1, y) + g(x + 1, y);
        u(x, y) = h(x, y - 1) + h(x, y + 1);
        v(x, y) = u(x - 1, y) + u(x + 1, y);

        u.compute_at(v, y);
        h.store_root().compute_at(v, y);
        g.compute_at(h, y);
        f.store_root().compute_at(v, y);

        v.realize({10, 10});
        if (count != 14 * 14) {
            printf("f was called %d times instead of %d times\n", count, 14 * 14);
            return 1;
        }
    }

    {
        // Sliding a func that has a boundary condition before the beginning
        // of the loop. This needs an explicit warmup before we start sliding.
        count = 0;
        Func f, g;
        f(x) = call_counter(x, 0);
        g(x) = f(max(x, 3));

        f.store_root().compute_at(g, x);

        g.realize({10});
        if (count != 7) {
            printf("f was called %d times instead of %d times\n", count, 7);
            return 1;
        }
    }

    {
        // Sliding a func that has a boundary condition on both sides.
        count = 0;
        Func f, g, h;
        f(x) = call_counter(x, 0);
        g(x) = f(clamp(x, 0, 9));
        h(x) = g(x - 1) + g(x + 1);

        f.store_root().compute_at(h, x);
        g.store_root().compute_at(h, x);

        h.realize({10});
        if (count != 10) {
            printf("f was called %d times instead of %d times\n", count, 10);
            return 1;
        }
    }

    {
        // Sliding in registers over two loops at once. A rolled register array
        // only carries values across the innermost loop it lives in, so f can
        // roll over x but not also over yi. If it tries to do both, the values
        // it expects to still be there from the previous yi were overwritten
        // by the sweep of the x loop.
        Var yo, yi;
        Func f, g;
        f(x, y) = x * 3 + y;
        g(x, y) = f(x, y) + f(x - 1, y) + f(x, y - 1);

        g.split(y, yo, yi, 4);
        f.store_at(g, yo).compute_at(g, x).store_in(MemoryType::Register);

        Buffer<int> im = g.realize({12, 12});
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int c = (x * 3 + y) + ((x - 1) * 3 + y) + (x * 3 + y - 1);
                if (im(x, y) != c) {
                    printf("g(%d, %d) = %d instead of %d\n", x, y, im(x, y), c);
                    return 1;
                }
            }
        }
    }

    {
        // A consumer that clamps its coordinate, so that the region required
        // of the producer is flat for a while and then starts moving. The loop
        // can't be rewound to warm this up, because rewinding it doesn't move
        // the window; it has to warm up on the first iteration instead.
        Func f, g;
        f(x, y) = x * 3 + y;
        g(x, y) = f(max(x, 4), y) + f(max(x, 4) + 2, y);

        f.store_root().compute_at(g, x);

        Buffer<int> im = g.realize({15, 15});
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int c = std::max(x, 4) * 3 + y + (std::max(x, 4) + 2) * 3 + y;
                if (im(x, y) != c) {
                    printf("g(%d, %d) = %d instead of %d\n", x, y, im(x, y), c);
                    return 1;
                }
            }
        }
    }

    {
        // Sliding a producer along the outer loop of a pair of outputs fused
        // together with compute_with. Previously triggered an internal compiler
        // error referencing a missing .loop_min symbol on the fused loop.
        count = 0;
        Func f, g1, g2;
        f(x, y) = call_counter(x, y);
        g1(x, y) = f(x, y - 1) + f(x, y + 1);
        g2(x, y) = f(x, y - 1) - f(x, y + 1);

        f.store_root().compute_at(g1, y);
        g2.compute_with(g1, x);

        Pipeline({g1, g2}).realize({10, 10});

        // f spans y in [-1, 10], so 12 rows of 10 = 120 calls when slid.
        if (count != 120) {
            printf("f was called %d times instead of %d times\n", count, 120);
            return 1;
        }
    }

    {
        // Sliding a cascade of filters. Halide needs bounds propagation
        // to prove that the innermost filters have monotonic bounds.
        count = 0;
        Func f1, f2, f3, f4, g;
        f1(x) = call_counter(x, 0);
        f2(x) = f1(0) + f1(x);
        f3(x) = f2(0) + f2(x);
        f4(x) = f3(0) + f3(x);
        g(x) = f4(0) + f4(x);
        f1.store_root().compute_at(g, x);
        f2.store_root().compute_at(g, x);
        f3.store_root().compute_at(g, x);
        f4.store_root().compute_at(g, x);
        g.bound(x, 0, 10);

        g.realize({10});
        // f1 spans x in [0, 9], so 10 calls when slid.
        if (count != 10) {
            printf("f1 was called %d times instead of %d times\n", count, 10);
            return 1;
        }
    }

    {
        // Two funcs sliding over the same loop, where one consumes the other,
        // and the outer func also consumes both. Both need to warm up their
        // windows, but over different numbers of iterations.
        // Sliding warms up its window by computing a little more than is
        // required at the edges, so give the input some slack.
        Buffer<int> input(9, 9);
        input.set_min(-4, -4);
        input.fill([](int x, int y) { return x * 100 + y; });

        Var yo, yi;
        Func f, g, h;
        f(x, y) = input(x, y);
        g(x, y) = (f(x + 1, y) + f(x - 1, y)) * 2;
        h(x, y) = (f(x + 1, y) + f(x - 1, y)) + (g(x + 1, y) + g(x - 1, y));

        h.never_partition_all().split(y, yo, yi, 1, TailStrategy::RoundUp);
        h.output_buffer().dim(0).set_bounds(0, 1).dim(1).set_bounds(0, 1);
        f.never_partition_all().store_at(h, yo).compute_at(h, x);
        g.never_partition_all().store_root().compute_at(h, x);

        Buffer<int> im = h.realize({1, 1});
        // f(x, y) = 100x + y, so g(x, y) = 400x + 4y, and
        // h(0, 0) = (100 + -100) + (400 + -400) = 0. Any coordinate mix-up
        // moves this, unlike a constant input.
        int correct = 0;
        if (im(0, 0) != correct) {
            printf("h(0, 0) = %d instead of %d\n", im(0, 0), correct);
            return 1;
        }
    }

    {
        // g is computed inside h's production and slides over the outermost
        // loop, while f and h both slide over yi and warm up their windows by
        // rewinding it. g has to start when h's warm-up does - any earlier and
        // it runs extra times, which corrupts its own sliding. Unlike the cases
        // above this one also passes without the fix; it's here for coverage of
        // the nesting, not as a regression test.
        const int size = 15;
        Var yo, yi;
        Buffer<int> ref;
        for (int slide = 0; slide < 2; slide++) {
            Func f, g, h, out;
            f(x, y) = x * 3 + y;
            g(x, y) = f(x * 2 - 1, y * 2 - 1) + f(x * 2, y * 2 + 2);
            h(x, y) = (f(x, y + 1) + f(x, y - 2)) +
                      (g(x / 2 + 2, y / 2 - 2) + g(x / 2 - 2, y / 2 + 2));
            out(x, y) = h(x * 2 + 1, y * 2 - 2) + h(x * 2 + 1, y * 2 + 2);

            if (slide) {
                out.split(y, yo, yi, 4, TailStrategy::RoundUp);
                f.store_at(out, yo).compute_at(out, yi);
                g.store_at(out, Var::outermost()).compute_at(h, Var::outermost());
                h.store_at(out, yo).compute_at(out, yi);
            } else {
                f.compute_root();
                g.compute_root();
                h.compute_root();
            }

            Buffer<int> im = out.realize({size, size});
            if (!slide) {
                ref = im;
            } else {
                for (int y = 0; y < size; y++) {
                    for (int x = 0; x < size; x++) {
                        if (im(x, y) != ref(x, y)) {
                            printf("out(%d, %d) = %d instead of %d\n",
                                   x, y, im(x, y), ref(x, y));
                            return 1;
                        }
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
