#include "Halide.h"
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

    printf("Success!\n");
    return 0;
}
