// Halide tutorial lesson 20: Cloning Funcs

// This lesson demonstrates how to use Func::clone_in to create a clone of
// a Func.

// On linux, you can compile and run it like so:
// g++ lesson_20*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_20 -std=c++11
// LD_LIBRARY_PATH=../bin ./lesson_20

// On os x:
// g++ lesson_20*.cpp -g -I ../include -L ../bin -lHalide -o lesson_20 -std=c++11
// DYLD_LIBRARY_PATH=../bin ./lesson_20

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_20_cloning_funcs
// in a shell at the top of the halide source tree.

// The only Halide header file you need is Halide.h. It includes all of Halide.
#include "Halide.h"

// We'll also include stdio for printf.
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // First we'll declare some Vars to use below.
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    // This lesson will be about cloning a Func using the Func::clone_in
    // directive.
    {
        // Consider a simple two-stage pipeline:
        Func f("f_single"), g("g_single"), h("h_single");
        f(x, y) = x + y;
        g(x, y) = 2 * f(x, y) + 3;
        h(x, y) = f(x, y) + g(x, y) + 10;

        f.compute_root();
        g.compute_root();
        h.compute_root();

        // This produces the following loop nests:
        // for y:
        //   for x:
        //     f(x, y) = x + y
        // for y:
        //   for x:
        //     g(x, y) = 2 * f(x, y) + 3
        // for y:
        //   for x:
        //     h(x, y) = f(x, y) + g(x, y) + 10

        // Using Func::clone_in, we can replace calls to 'f' inside 'g' with
        // a clone of 'f' using the schedule alone:
        Func f_clone_in_g = f.clone_in(g);
        f_clone_in_g.compute_root();

        // Equivalently, we could also chain the schedules like so:
        // f.clone_in(g).compute_root();

        // This produces the following loop nests:
        // for y:
        //   for x:
        //     f(x, y) = x + y
        // for y:
        //   for x:
        //     f_clone_in_g(x, y) = x + y
        // for y:
        //   for x:
        //     g(x, y) = 2 * f_clone_in_g(x, y) + 3
        // for y:
        //   for x:
        //     h(x, y) = f(x, y) + g(x, y) + 10

        h.realize(5, 5);

        // The schedule directive f.clone_in(g) replaces all calls to 'f'
        // inside 'g' with a clone of 'f' and then returns that clone.
        // Essentially, it rewrites the original pipeline above into the
        // following:
        {
            Func f_clone_in_g("f_clone_in_g"), f("f"), g("g"), h("h");
            f(x, y) = x + y;
            f_clone_in_g(x, y) = x + y;
            g(x, y) = 2 * f_clone_in_g(x, y) + 3;
            h(x, y) = f(x, y) + g(x, y) + 10;

            f.compute_root();
            f_clone_in_g.compute_root();
            g.compute_root();
            h.compute_root();
        }
    }

    {
        // In the schedule above, only the calls to 'f' made by 'g' are
        // replaced. Other calls made to 'f' would still call 'f' directly
        // (i.e. 'h' still calls 'f' and not the clone). If we wish to
        // replace all calls to 'f' made by both 'g' and 'h' with a single
        // clone, we simply say f.clone_in({g, h}).

        // Consider a three stage pipeline, with two consumers of f:
        Func f("f_group"), g("g_group"), h("h_group"), out("out_group");
        f(x, y) = x + y;
        g(x, y) = 2 * f(x, y);
        h(x, y) = f(x, y) + 10;
        out(x, y) = f(x, y) + g(x, y) + h(x, y);

        f.compute_root();
        g.compute_root();
        h.compute_root();
        out.compute_root();

        // We will replace all calls to 'f' inside both 'g' and 'h'
        // with calls to a single clone:
        f.clone_in({g, h}).compute_root();

        // The equivalent loop nests are:
        // for y:
        //   for x:
        //     f(x, y) = x + y
        // for y:
        //   for x:
        //     f_clone(x, y) = x + y
        // for y:
        //   for x:
        //     g(x, y) = 2 * f_clone(x, y)
        // for y:
        //   for x:
        //     h(x, y) = f_clone(x, y) + 10
        // for y:
        //   for x:
        //     out(x, y) = f(x, y) + g(x, y) + h(x, y)

        out.realize(5, 5);
    }

    {
        // One use case of Func::clone_in() is when two consumers of a producer
        // consume regions of the producer that are very disjoint. Consider
        // the following case for example:
        Func f("f"), g("g"), h("h");
        f(x) = x;
        g(x) = 2 * f(0);
        h(x) = f(99) + 10;

        // Let's schedule 'f' to be computed at root.
        f.compute_root();
        // Since both 'g' and 'h' consume 'f', the region required of 'f'
        // in the x-dimension is [0, 99]. The equivalent loop nests are:
        // for x = 0 to 99
        //   f(x) = x
        // for x:
        //   g(x) = 2 * f(0)
        // for x:
        //   h(x) = f(99) + 10

        // If 'f' is very expensive to compute, we might be better off with
        // having distinct copies of 'f' for each consumer, 'g' and 'h', to
        // avoid unnecessary computations. To create separate copies of 'f'
        // for each consumer, we can do the following:
        f.clone_in(g).compute_root();

        // The equivalent loop nests are:
        // f(0) = x
        // f_clone(99) = x
        // for x:
        //   g(x) = 2 * f_clone(0)
        // for x:
        //   h(x) = f(99) + 10
    }

    printf("Success!\n");

    return 0;
}
