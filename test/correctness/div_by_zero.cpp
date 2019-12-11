#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

template<typename T>
void test() {
    // Division by zero in Halide is defined to return zero, and
    // division by the most negative integer by -1 returns the most
    // negative integer. To preserve the Euclidean identity, this
    // means that x % 0 == x.

    Type t = halide_type_of<T>();

    // First test that the simplifier knows this:
    Expr zero = cast<T>(0);
    Expr x = Variable::make(t, unique_name('t'));

    Expr test = simplify(x / zero == zero);
    _halide_user_assert(is_one(test)) << test << '\n';
    test = simplify(x % zero == x);
    _halide_user_assert(is_one(test)) << test << '\n';

    if (t.is_int() && t.bits() < 32) {
        test = simplify(t.min() / cast<T>(-1) == t.min());
        _halide_user_assert(is_one(test)) << simplify(t.min() / cast<T>(-1)) << " vs " << t.min() << '\n';
        // Given the above decision, the following is required for
        // the Euclidean identity to hold:
        test = simplify(t.min() % cast<T>(-1) == zero);
        _halide_user_assert(is_one(test)) << test << '\n';
    }

    // Now check that codegen does the right thing:
    Param<T> a, b;
    a.set(T{5});
    b.set(T{0});
    T result = evaluate<T>(a / b);
    _halide_user_assert(result == T{0}) << result << '\n';
    result = evaluate<T>(a % b);
    _halide_user_assert(result == T{5}) << result << '\n';
    if (t.is_int() && t.bits() < 32) {
        uint64_t bits = 1;
        bits <<= (t.bits() - 1);
        T min_val;
        memcpy(&min_val, &bits, sizeof(min_val));
        a.set(min_val);
        b.set(T(-1));
        result = evaluate<T>(a / b);
        _halide_user_assert(result == min_val) << result << '\n';
        result = evaluate<T>(a % b);
        _halide_user_assert(result == T{0}) << result << '\n';
    }
}

int main(int argc, char **argv) {
    test<uint8_t>();
    test<int8_t>();
    test<uint16_t>();
    test<int16_t>();
    test<uint32_t>();
    test<int32_t>();

    // Here's a case that illustrates why it's important to have
    // defined behavior for division by zero:

    Func f;
    Var x;
    f(x) = 256 / (x + 1);
    Var xo, xi;
    f.vectorize(x, 8, TailStrategy::ShiftInwards);

    f.realize(5);

    // Ignoring scheduling, we're only realizing f over positive
    // values of x, so this shouldn't fault. However scheduling can
    // over-compute. In this case, vectorization with ShiftInwards
    // results in evaluating smaller values of x, including zero. This
    // would fault at runtime if we didn't have defined behavior for
    // division by zero.

    return 0;
}
