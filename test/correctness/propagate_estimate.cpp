#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {

void check(const Expr &input, Expr expected) {
    Expr result = simplify(substitute_var_estimates(input));
    expected = simplify(expected);
    if (!equal(result, expected)) {
        internal_error
            << "\nsubstitute_var_estimates() failure:\n"
            << "Input: " << input << "\n"
            << "Result: " << result << "\n"
            << "Expected result: " << expected << "\n";
    }
}

}  // namespace

int main(int argc, char **argv) {
    Param<int> p;
    p.set_estimate(10);

    ImageParam img(Int(32), 2);
    img.dim(0).set_estimate(-3, 33);
    img.dim(1).set_estimate(5, 55);

    Var x("x"), y("y");
    check(p + x + y, x + y + 10);
    check(img.dim(0).min() + img.dim(1).min() + x, x + 2);
    check(img.dim(0).extent() + img.dim(1).min() + img.dim(1).extent() * x, 55 * x + 38);

    printf("Success!\n");
    return 0;
}
