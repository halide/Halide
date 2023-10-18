#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);
        g.compute_root()
            .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

        // f.compute_at(g, xo)
        //     .hoist_storage(g, yo);

        // g.compute_root()
        //     .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

        f.compute_at(g, xo)
            .hoist_storage(g, Var::outermost())
            .bound_storage(x, 18)
            .bound_storage(y, 18);

        Buffer<int> out = g.realize({128, 128});

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // {
    //     Buffer<int> input(64, 64);
    //     Func f("f"), g("g");
    //     Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    //     // f(x, y) = input(x, y);
    //     f = BoundaryConditions::repeat_edge(input);
    //     g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);
    //     g.compute_root()
    //         .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

    //     f.compute_at(g, xo)
    //         .hoist_storage(g, yo);

    //     // g.compute_root()
    //     //     .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

    //     // f.compute_at(g, xo)
    //     //     .hoist_storage(g, Var::outermost());

    //     Buffer<int> out = g.realize({128, 128});

    //     out.for_each_element([&](int x, int y) {
    //         int correct = 2 * (x + y);
    //         if (out(x, y) != correct) {
    //             printf("out(%d, %d) = %d instead of %d\n",
    //                    x, y, out(x, y), correct);
    //             exit(1);
    //         }
    //     });
    // }

    printf("Success!\n");
    return 0;
}