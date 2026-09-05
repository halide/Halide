#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Note: this test is built with NDEBUG, so assert() compiles to nothing.
bool check(bool ok, const char *msg) {
    if (!ok) {
        printf("Failed: %s\n", msg);
    }
    return ok;
}

class LoopExtents : public IRVisitor {
    using IRVisitor::visit;

    void visit(const For *op) override {
        extents.push_back(simplify(op->extent()));
        IRVisitor::visit(op);
    }

public:
    std::vector<Expr> extents;
};

class CountMod : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Mod *op) override {
        count++;
        IRVisitor::visit(op);
    }

public:
    int count{0};
};

}  // namespace

int main(int argc, char **argv) {
    // A loop of eight elements whose first and last iterations are special,
    // and whose interior is periodic with period two. Unrolling the interior
    // by two turns the % into a constant, but only if the unrolled pairs line
    // up with the periodicity -- which means the tiles have to start at x=1,
    // where the interior begins, not at x=0.
    //
    // An aligned split expresses exactly that: split by two, anchored at one.
    // Loop partitioning then peels the one iteration at each end that the
    // likely() marks as not-steady-state, leaving x in [1, 6]. That's six
    // iterations, or three of the unrolled-by-two loop.
    //
    // Without the alignment the tiles start at x=0 instead, the interior
    // doesn't fill a whole number of them, and partitioning has to peel two
    // iterations at each end rather than one -- leaving a steady-state loop
    // of two rather than three.
    Var x{"x"}, xo{"xo"}, xi{"xi"};
    Func f{"f"};
    f(x) = select(x <= 0, 100,
                  x < 7, likely(x % 2),
                  200);
    f.bound(x, 0, 8);
    f.split(x, xo, xi, 2, 1, TailStrategy::GuardWithIf)
        .always_partition(xo)
        .unroll(xi);

    Module m = f.compile_to_module({}, "f");

    LoopExtents loops;
    CountMod mods;
    for (const LoweredFunc &lf : m.functions()) {
        lf.body.accept(&loops);
        lf.body.accept(&mods);
    }

    printf("Loops:");
    for (const Expr &e : loops.extents) {
        std::cout << " " << e;
    }
    printf("\n");

    // The two peeled iterations are single elements, so they come out as
    // straight-line code rather than loops. What's left is the steady state.
    if (!check(loops.extents.size() == 1, "expected exactly one remaining loop")) {
        return 1;
    }
    if (!check(is_const(loops.extents[0], 3),
               "expected the steady-state loop to run three times (six "
               "elements, unrolled by two)")) {
        return 1;
    }

    // The whole point of unrolling the interior was to fold away the %.
    if (!check(mods.count == 0, "expected the modulo to fold away")) {
        return 1;
    }

    Buffer<int> out = f.realize({8});
    for (int i = 0; i < 8; i++) {
        int expected = 200;
        if (i <= 0) {
            expected = 100;
        } else if (i < 7) {
            expected = i % 2;
        }
        if (out(i) != expected) {
            printf("out(%d) = %d instead of %d\n", i, out(i), expected);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
