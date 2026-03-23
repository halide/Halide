#include "Halide.h"
using namespace Halide;

void *my_malloc(JITUserContext *user_context, size_t x) {
    printf("There was not supposed to be a heap allocation\n");
    exit(1);
    return nullptr;
}

void my_free(JITUserContext *user_context, void *ptr) {
}

bool errored = false;
void my_error(JITUserContext *user_context, const char *msg) {
    errored = true;
    char expected[] = "Bounds given for f in x (from 0 to 7) do not cover required region (from 0 to 9)";
    if (strncmp(expected, msg, sizeof(expected) - 1)) {
        printf("Unexpected error: '%s'\n", msg);
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    {
        Func f("f"), g;
        Var x("x"), xo, xi;

        Param<int> p;

        f(x) = x;
        g(x) = f(x);
        g.split(x, xo, xi, p);

        // We need p elements of f per split of g. This could create a
        // dynamic allocation. Instead we'll assert that 8 is enough, so
        // that f can go on the stack and be entirely vectorized.
        f.compute_at(g, xo).bound_extent(x, 8).vectorize(x);

        // Check there's no malloc when the bound is good
        g.jit_handlers().custom_malloc = my_malloc;
        g.jit_handlers().custom_free = my_free;
        p.set(5);
        g.realize({20});
        g.jit_handlers().custom_malloc = nullptr;
        g.jit_handlers().custom_free = nullptr;

        // Check there was an assertion failure of the appropriate type when the bound is violated
        g.jit_handlers().custom_error = my_error;
        p.set(10);
        g.realize({20});

        if (!errored) {
            printf("There was supposed to be an error\n");
            return 1;
        }
    }

    for (TailStrategy tail_strategy : {TailStrategy::GuardWithIf, TailStrategy::Predicate, TailStrategy::PredicateLoads}) {
        // Another way in which a larger static allocation is
        // preferable to a smaller dynamic one is when you compute
        // something at a split guarded by an if. In the very last
        // split (the tail) you don't actually need the whole split's
        // worth of the producer, and indeed asking for it may expand
        // the bounds required of an input image.
        Func f, g;
        Var x, xo, xi;

        f(x) = x;
        g(x) = f(x);
        g.split(x, xo, xi, 8, tail_strategy);

        f.compute_at(g, xo);
        // In the tail case, the amount of g required is min(8, some
        // nasty thing), so we'll add a bound.
        f.bound_extent(x, 8);

        g.jit_handlers().custom_malloc = my_malloc;
        g.jit_handlers().custom_free = my_free;
        g.realize({20});
    }

    printf("Success!\n");
    return 0;
}
