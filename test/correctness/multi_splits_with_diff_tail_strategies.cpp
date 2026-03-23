#include "Halide.h"
#include "check_call_graphs.h"

using namespace Halide;

int main(int argc, char **argv) {
    // ApplySplit should respect the order of the application of substitutions/
    // predicates/lets; otherwise, this combination of tail strategies will
    // cause an access out of bound error.
    for (TailStrategy tail_strategy : {TailStrategy::GuardWithIf, TailStrategy::Predicate, TailStrategy::PredicateLoads}) {
        Func f("f"), input("input");
        Var x("x"), y("y"), c("c");

        f(x, y, c) = x + y + c;

        f.reorder(c, x, y);
        Var yo("yo"), yi("yi");
        f.split(y, yo, yi, 2, TailStrategy::RoundUp);

        Var yoo("yoo"), yoi("yoi");
        f.split(yo, yoo, yoi, 64, tail_strategy);

        Buffer<int> im = f.realize({3000, 2000, 3});
        auto func = [](int x, int y, int c) {
            return x + y + c;
        };
        if (check_image(im, func)) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
