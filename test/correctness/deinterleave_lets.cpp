#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Expr x = Variable::make(Int(32), "x");

    // rewrite_interleavings gives every vector let a pair of extra lets holding
    // its even and odd lanes. Those hold half as many lanes as the let they
    // come from, so a deinterleave that wants some other number of lanes must
    // not be rewritten to use them. Here b reads two of a's four lanes, so
    // b.even_lanes wants a single lane, which a.even_lanes cannot provide.
    Expr a = Variable::make(Int(32).with_lanes(4), "a");
    Expr b = Variable::make(Int(32).with_lanes(2), "b");

    Stmt s = Store::make("buf", b, Ramp::make(x, 1, 2));
    s = LetStmt::make("b", Shuffle::make({a}, {0, 2}), s);
    s = LetStmt::make("a", Ramp::make(x, 1, 4), s);

    // Simplifying checks that every variable matches the type of the let that
    // defines it.
    simplify(rewrite_interleavings(s));

    printf("Success!\n");
    return 0;
}
