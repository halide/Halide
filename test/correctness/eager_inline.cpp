#include "Halide.h"
using namespace Halide;

// eager_inline() performs the substitution immediately, so the caller's
// definition no longer references the inlined Funcs (they are inlined by value).
// Verify the numerics of a simple chained inline match a plain inlined pipeline.

int main(int argc, char **argv) {
    Var x{"x"};
    Func a{"a"}, b{"b"}, c{"c"};
    a(x) = x + 1;
    b(x) = a(x) * 2;     // calls a
    c(x) = b(x) + a(x);  // calls b (which calls a) and a directly

    // Inline b then a into c. Inlining b splices in its call to a, which the
    // subsequent inline of a then also folds.
    c.eager_inline({b, a});

    Expr c_body = c.function().definition().values()[0];
    Expr c_expected = x * 3 + 3;
    internal_assert(Internal::can_prove(c_body == c_expected))
        << "eager_inline chain failed to fold all calls to a and b into c\n"
        << "Saw: " << c_body << "\nExpected: " << c_expected << "\n";

    Buffer<int> out = c.realize({8});
    for (int i = 0; i < 8; i++) {
        int ref = (i + 1) * 2 + (i + 1);
        if (out(i) != ref) {
            printf("eager_inline chain mismatch at %d: %d vs %d\n", i, out(i), ref);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
