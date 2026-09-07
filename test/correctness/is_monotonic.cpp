#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {

void check_increasing(const Expr &e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Increasing)
        << "Was supposed to be increasing: " << e << "\n";
}

void check_decreasing(const Expr &e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Decreasing)
        << "Was supposed to be decreasing: " << e << "\n";
}

void check_constant(const Expr &e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Constant)
        << "Was supposed to be constant: " << e << "\n";
}

void check_unknown(const Expr &e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Unknown)
        << "Was supposed to be unknown: " << e << "\n";
}

}  // namespace

int main() {
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    Expr z = Variable::make(Int(32), "z");

    check_increasing(x);
    check_increasing(x + 4);
    check_increasing(x + y);
    check_increasing(x * 4);
    check_increasing(x / 4);
    check_increasing(min(x + 4, y + 4));
    check_increasing(max(x + y, x - y));
    check_increasing(x >= y);
    check_increasing(x > y);

    check_decreasing(-x);
    check_decreasing(x * -4);
    check_decreasing(x / -4);
    check_decreasing(y - x);
    check_decreasing(x < y);
    check_decreasing(x <= y);

    check_unknown(x == y);
    check_unknown(x != y);
    check_increasing(y <= x);
    check_increasing(y < x);
    check_decreasing(x <= y);
    check_decreasing(x < y);
    check_unknown(x * y);

    // Not constant despite having constant args, because there's a side-effect.
    check_unknown(Call::make(Int(32), "foo", {Expr(3)}, Call::Extern));

    check_increasing(select(y == 2, x, x + 4));
    check_decreasing(select(y == 2, -x, x * -4));

    check_unknown(select(x > 2, x - 2, x));
    check_unknown(select(x < 2, x, x - 2));
    check_unknown(select(x > 2, -x + 2, -x));
    check_unknown(select(x < 2, -x, -x + 2));
    check_increasing(select(x > 2, x - 1, x));
    check_increasing(select(x < 2, x, x - 1));
    check_decreasing(select(x > 2, -x + 1, -x));
    check_decreasing(select(x < 2, -x, -x + 1));

    check_unknown(select(x < 2, x, x - 5));
    check_unknown(select(x > 2, x - 5, x));

    check_unknown(select(x > 0, y, z));

    check_increasing(select(0 < x, promise_clamped(x - 1, x - 1, z) + 1, promise_clamped(x, x, z)));

    check_constant(y);

    check_increasing(select(x < 17, y, y + 1));
    check_increasing(select(x > 17, y, y - 1));
    check_decreasing(select(x < 17, y, y - 1));
    check_decreasing(select(x > 17, y, y + 1));

    check_increasing(select(x % 2 == 0, x + 3, x + 3));

    check_constant(select(y > 3, y + 23, y - 65));

    check_decreasing(select(2 <= x, 0, 1));
    check_increasing(select(2 <= x, 0, 1) + x);
    check_decreasing(-min(x, 16));

    check_unknown(select(0 < x, max(min(x, 4), 3), 4));

    printf("Success!\n");
    return 0;
}
