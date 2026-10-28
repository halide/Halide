#include "Halide.h"
#include <iostream>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

class CountShuffles : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Shuffle *op) override {
        count++;
        IRVisitor::visit(op);
    }

public:
    int count = 0;
};

int main(int argc, char **argv) {
    // A dot product of 16 elements into 4 lanes (Arm's sdot shape), where one
    // factor is uniform over each group of 8 elements: k / 8 vectorizes as
    // ramp(x4(0), x4(1), 4) / x16(2). Splitting the store by 2 doesn't make
    // that ramp's lanes contiguous, and the halves of the reduction no longer
    // match a dot-product instruction, so rewrite_interleavings must not
    // deinterleave for a div or mod of a ramp with a vector base.
    Expr x = Variable::make(Int(32), "x");
    Expr v = Variable::make(Int(32).with_lanes(16), "v");
    Expr k = Ramp::make(Broadcast::make(0, 4), Broadcast::make(1, 4), 4);

    for (bool is_div : {true, false}) {
        Expr g = is_div ? k / Broadcast::make(2, 16) : k % Broadcast::make(2, 16);
        Expr dot = VectorReduce::make(VectorReduce::Add, v * g, 4);
        Stmt s = Store::make("out", dot, Ramp::make(x, 1, 4));
        s = rewrite_interleavings(s);

        CountShuffles shuffles;
        s.accept(&shuffles);
        if (shuffles.count != 0) {
            std::cout << "The " << (is_div ? "div" : "mod")
                      << " of a ramp with a vector base was deinterleaved:\n"
                      << s << "\n";
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
