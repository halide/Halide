#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {

void check(const Expr &e, int64_t m, int64_t r) {
    ModulusRemainder result = modulus_remainder(e);
    if (result.modulus != m || result.remainder != r) {
        std::cerr << "Test failed for modulus_remainder:\n";
        std::cerr << "Expression: " << e << "\n";
        std::cerr << "Correct modulus, remainder  = " << m << ", " << r << "\n";
        std::cerr << "Computed modulus, remainder = "
                  << result.modulus << ", "
                  << result.remainder << "\n";
        exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    check((30 * x + 3) + (40 * y + 2), 10, 5);
    check((6 * x + 3) * (4 * y + 1), 2, 1);
    check(max(30 * x - 24, 40 * y + 31), 5, 1);
    check(10 * x - 33 * y, 1, 0);
    check(10 * x - 35 * y, 5, 0);
    check(123, 0, 123);
    check(Let::make("y", x * 3 + 4, y * 3 + 4), 9, 7);
    // Check overflow
    check((5045320 * x + 4) * (405713 * y + 3) * (8000123 * x + 4354), 1, 0);

    printf("Success!\n");
    return 0;
}
