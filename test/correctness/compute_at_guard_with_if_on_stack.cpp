#include "Halide.h"

using namespace Halide;

void *my_malloc(void *user_context, size_t x) {
    printf("This pipeline was not supposed to call malloc\n");
    abort();
    return nullptr;
}

void my_free(void *user_context, void *ptr) {
}

int main(int argc, char **argv) {
    Func f, g;
    Var x;


    f(x) = x;
    g(x) = f(x);

    Var xo, xi;
    g.split(x, xo, xi, 8, TailStrategy::GuardWithIf);
    f.compute_at(g, xo);

    g.set_custom_allocator(my_malloc, my_free);
    g.realize(20);

    return 0;
}
