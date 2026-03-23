#include "Halide.h"

using namespace Halide;

size_t largest_allocation = 0;

void *my_malloc(JITUserContext *user_context, size_t x) {
    largest_allocation = std::max(x, largest_allocation);
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(JITUserContext *user_context, void *ptr) {
    free(((void **)ptr)[-1]);
}

void check(Func out, int line, std::vector<TailStrategy> tails) {
    bool has_round_up =
        std::find(tails.begin(), tails.end(), TailStrategy::RoundUp) != tails.end() ||
        std::find(tails.begin(), tails.end(), TailStrategy::RoundUpAndBlend) != tails.end() ||
        std::find(tails.begin(), tails.end(), TailStrategy::PredicateLoads) != tails.end() ||
        std::find(tails.begin(), tails.end(), TailStrategy::PredicateStores) != tails.end();
    bool has_shift_inwards =
        std::find(tails.begin(), tails.end(), TailStrategy::ShiftInwards) != tails.end() ||
        std::find(tails.begin(), tails.end(), TailStrategy::ShiftInwardsAndBlend) != tails.end();

    std::vector<int> sizes_to_try;

    // A size that's a multiple of all the splits should always be
    // exact
    sizes_to_try.push_back(1024);

    // Sizes larger than any of the splits should be fine if we don't
    // have any roundups. The largest split we have is 128
    if (!has_round_up) {
        sizes_to_try.push_back(130);
    }

    // Tiny sizes are fine if we only have GuardWithIf
    if (!has_round_up && !has_shift_inwards) {
        sizes_to_try.push_back(3);
    }

    out.jit_handlers().custom_malloc = my_malloc;
    out.jit_handlers().custom_free = my_free;

    for (int s : sizes_to_try) {
        largest_allocation = 0;
        out.realize({s});
        size_t expected = (s + 1) * 4;
        size_t tolerance = 3 * sizeof(int);
        if (largest_allocation > expected + tolerance) {
            std::cerr << "Failure on line " << line << "\n"
                      << "with tail strategies: ";
            for (auto t : tails) {
                std::cerr << t << " ";
            }
            std::cerr << "\n allocation of " << largest_allocation
                      << " bytes is too large. Expected " << expected + tolerance << "\n";
            abort();
        }
    }
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    // We'll randomly subsample these tests, because otherwise there are too many of them.
    std::mt19937 rng(0);
    int seed = argc > 1 ? atoi(argv[1]) : time(nullptr);
    rng.seed(seed);
    std::cout << "Nested tail strategies seed: " << seed << "\n";

    // Test random compositions of tail strategies in simple
    // producer-consumer pipelines. The bounds being tight sometimes
    // depends on the simplifier being able to cancel out things.

    TailStrategy tails[] = {
        TailStrategy::RoundUp,
        TailStrategy::GuardWithIf,
        TailStrategy::ShiftInwards,
        TailStrategy::RoundUpAndBlend,
        TailStrategy::ShiftInwardsAndBlend};

    TailStrategy innermost_tails[] = {
        TailStrategy::RoundUp,
        TailStrategy::GuardWithIf,
        TailStrategy::PredicateLoads,
        TailStrategy::PredicateStores,
        TailStrategy::ShiftInwards,
        TailStrategy::RoundUpAndBlend,
        TailStrategy::ShiftInwardsAndBlend};

    // Two stages. First stage computed at tiles of second.
    for (auto t1 : innermost_tails) {
        for (auto t2 : innermost_tails) {
            Func in, f, g;
            Var x;

            in(x) = x;
            f(x) = in(x);
            g(x) = f(x);

            Var xo, xi;
            g.split(x, xo, xi, 64, t1);
            f.compute_at(g, xo).split(x, xo, xi, 8, t2);
            in.compute_root();

            check(g, __LINE__, {t1, t2});
        }
    }

    // Three stages. First stage computed at tiles of second, second
    // stage computed at tiles of third.
    for (auto t1 : innermost_tails) {
        for (auto t2 : innermost_tails) {
            for (auto t3 : innermost_tails) {
                if ((rng() & 7) != 0) {
                    continue;
                }

                Func in("in"), f("f"), g("g"), h("h");
                Var x;

                in(x) = x;
                f(x) = in(x);
                g(x) = f(x);
                h(x) = g(x);

                Var xo, xi;
                h.split(x, xo, xi, 64, t1);
                g.compute_at(h, xo).split(x, xo, xi, 16, t2);
                f.compute_at(g, xo).split(x, xo, xi, 4, t3);
                in.compute_root();

                check(h, __LINE__, {t1, t2, t3});
            }
        }
    }

    // Three stages. First stage computed at tiles of third, second
    // stage computed at smaller tiles of third.
    for (auto t1 : tails) {
        for (auto t2 : innermost_tails) {
            for (auto t3 : innermost_tails) {
                if ((rng() & 7) != 0) {
                    continue;
                }

                Func in, f, g, h;
                Var x;

                in(x) = x;
                f(x) = in(x);
                g(x) = f(x);
                h(x) = g(x);

                Var xo, xi, xii, xio;
                h.split(x, xo, xi, 128, t1).split(xi, xio, xii, 64);
                g.compute_at(h, xio).split(x, xo, xi, 8, t2);
                f.compute_at(h, xo).split(x, xo, xi, 8, t3);
                in.compute_root();

                check(h, __LINE__, {t1, t2, t3});
            }
        }
    }

    // Same as above, but the splits on the output are composed in
    // reverse order so we don't get a perfect split on the inner one
    // (but can handle smaller outputs).
    for (auto t1 : innermost_tails) {
        for (auto t2 : tails) {
            for (auto t3 : innermost_tails) {
                for (auto t4 : tails) {
                    if ((rng() & 63) != 0) {
                        continue;
                    }

                    Func in("in"), f("f"), g("g"), h("h");
                    Var x;

                    in(x) = x;
                    f(x) = in(x);
                    g(x) = f(x);
                    h(x) = g(x);

                    Var xo, xi, xoo, xoi;
                    h.split(x, xo, xi, 64, t1).split(xo, xoo, xoi, 2, t2);
                    g.compute_at(h, xoi).split(x, xo, xi, 8, t3);
                    f.compute_at(h, xoo).split(x, xo, xi, 8, t4);
                    in.compute_root();

                    check(h, __LINE__, {t1, t2, t3, t4});
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
