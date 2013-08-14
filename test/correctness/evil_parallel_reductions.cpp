#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, y;
    RDom r(1, 255, "r");

    // Halide by default places loops over reduction dimensions
    // outermost, and you can't reorder them without possibly changing
    // the meaning. If you really really want to reorder reduction
    // dimensions, and you either know it won't change the meaning in
    // your case, or you don't mind a few race conditions, then you
    // can treat an RVar as a Var by constructing a Var with the same
    // name. This is evil, and you shouldn't do it.

    f(x, y) = x+y;
    f(r, y) += f(r-1, y);

    f.parallel(y);
    f.update().parallel(y).reorder(Var(r.x.name()), y);

    f.realize(256, 256);

    printf("Success!\n");

    return 0;

}
