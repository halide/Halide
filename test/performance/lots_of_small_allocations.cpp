#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Param<int> p;

    const char *names[3] = {"heap", "pseudostack", "stack"};

    double t[3];
    for (int i = 0; i < 3; i++) {
        Var x("x");

        Func in;
        in(x) = x;

        std::vector<Func> chain;
        chain.push_back(in);
        for (int j = 0; j < 50; j++) {
            Func next;
            // Iterate the Collatz conjecture
            Expr prev = chain.back()(x);
            next(x) = select(prev % 2 == 0, prev / 2, 3 * prev + 1);
            chain.push_back(next);
        }

        Var xo, xi;
        chain.back().split(x, xo, xi, p, TailStrategy::RoundUp);
        for (size_t j = 0; j < chain.size() - 1; j++) {
            chain[j].compute_at(chain.back(), xo);
            if (i != 0) {
                chain[j].store_in(MemoryType::Stack);
            }
            if (i == 2) {
                chain[j].bound_extent(x, p);
            }
            // Vectorize. Otherwise llvm autovectorizes the stack version, confusing the results
            chain[j].vectorize(x, 8, TailStrategy::RoundUp);
        }
        // One of the problems with frequent heap allocations is that
        // they can serialize in the allocator, so we should
        // parallelize things too.
        Var xoo;
        if (i == 2) {
            chain.back().specialize(p == 200).split(xo, xoo, xo, 100, TailStrategy::RoundUp).parallel(xoo);
            chain.back().specialize_fail("Expected p == 200");
        } else {
            chain.back().split(xo, xoo, xo, 100, TailStrategy::RoundUp).parallel(xoo);
        }
        chain.back().vectorize(xi, 8, TailStrategy::RoundUp);

        // Make it too large for llvm to promote into registers or
        // bother unrolling. We're trying to compare stack to
        // pseudostack, not stack to register.
        p.set(200);

        Buffer<int> out(16 * 1000 * 1000);
        t[i] = Halide::Tools::benchmark([&] { chain.back().realize(out); });

        printf("Time using %s: %f\n", names[i], t[i]);
    }

    if (t[0] < t[1]) {
        printf("Heap allocation was faster than pseudostack!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
