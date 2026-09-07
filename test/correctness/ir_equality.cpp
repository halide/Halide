#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {

void check_equal(const Expr &a, const Expr &b) {
    internal_assert(graph_equal(a, b))
        << "Error in ir_equality test: expected equal, but not equal, when comparing:\n"
        << a
        << "\nand\n"
        << b << "\n";
}

void check_not_equal(const Expr &a, const Expr &b) {
    bool eq = graph_equal(a, b);
    bool lt_ab = graph_less_than(a, b);
    bool lt_ba = graph_less_than(b, a);
    internal_assert(!eq && (lt_ab != lt_ba))
        << "Error in ir_equality test: expected not equal with a consistent "
           "(antisymmetric) ordering, when comparing:\n"
        << a
        << "\nand\n"
        << b << "\n";
}

}  // namespace

int main() {
    Expr x = Variable::make(Int(32), "x");
    check_equal(Ramp::make(x, 4, 3), Ramp::make(x, 4, 3));
    check_not_equal(Ramp::make(x, 2, 3), Ramp::make(x, 4, 3));

    check_equal(x, Variable::make(Int(32), "x"));
    check_not_equal(x, Variable::make(Int(32), "y"));

    // Something that will hang if IREquality has poor computational
    // complexity.
    Expr e1 = x, e2 = x;
    for (int i = 0; i < 100; i++) {
        e1 = e1 * e1 + e1;
        e2 = e2 * e2 + e2;
    }
    check_equal(e1, e2);
    // These are only discovered to be not equal way down the tree:
    e2 = e2 * e2 + e2;
    check_not_equal(e1, e2);

    printf("Success!\n");
    return 0;
}
