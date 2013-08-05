#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    // A single-dimensional argmax.
    Func f, arg_max_f;
    Var x;

    f(x) = x * (100 - x);

    RDom r(0, 100);
    arg_max_f() = 0;
    // The clamp is necessary, because otherwise we'll be indexing f
    // at a location determined by a load from a Halide image, and we
    // don't infer anything about how large that could be.
    Expr best_so_far = f(clamp(arg_max_f(), 0, 100));
    arg_max_f() = select(f(r) > best_so_far, r, arg_max_f());
    f.compute_root();

    Image<int> result_f = arg_max_f.realize();

    printf("%d\n", result_f(0));


    // Now try a multi-dimensional argmax.
    Func g, arg_max_g;
    Var y, c;
    r = RDom(0, 100, 0, 100);
    g(x, y) = x * (100 - x) + y * (80 - y);
    g.compute_root();

    arg_max_g() = Tuple(0, 0, g(0,0));
    best_so_far = g(clamp(arg_max_g()[0], 0, 99), clamp(arg_max_g()[1], 0, 99));
    arg_max_g() = tuple_select(g(r.x, r.y) > arg_max_g()[2],
                               Tuple(r.x, r.y, g(r.x, r.y)),
                               arg_max_g());

    Realization result_g = arg_max_g.realize();
    int best_x = Image<int>(result_g[0])(0);
    int best_y = Image<int>(result_g[1])(0);

    printf("%d %d\n", best_x, best_y);

    if (result_f(0) != 50) {
        printf("Arg max of f is %d, but should have been 50\n", result_f(0));
        return -1;
    }

    if (best_x != 50 || best_y != 40) {
        printf("Arg max of g is %d, %d, but should have been 50, 40\n",
               best_x, best_y);
        return -1;
    }

    return 0;
}
