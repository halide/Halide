#include "Halide.h"
#include <sstream>
#include <string>

using namespace Halide;

// eager_inline() performs the substitution immediately, so the caller's
// definition no longer references the inlined Funcs (they are inlined by value).
// Verify the numerics of a simple chained inline match a plain inlined pipeline.

namespace {

// Does the printed form of `e` contain a direct call to Func `name`?
bool mentions(const Expr &e, const std::string &name) {
    std::ostringstream os;
    os << e;
    return os.str().find(name + "(") != std::string::npos;
}

}  // namespace

int main(int argc, char **argv) {
    Var x{"x"};
    Func a{"a"}, b{"b"}, c{"c"};
    a(x) = x + 1;
    b(x) = a(x) * 2;     // calls a
    c(x) = b(x) + a(x);  // calls b (which calls a) and a directly

    // Inline b then a into c. Inlining b splices in its call to a, which the
    // subsequent inline of a then also folds.
    c.eager_inline(b, a);

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

    // eager_inline() is stage-scoped: inlining into one stage leaves the other
    // definitions of the same Func untouched.
    {
        RDom r(0, 4);

        // Stage-level: inline into the update only; the init definition still
        // calls prod.
        Func prod{"prod"}, f{"f"};
        prod(x) = x + 1;
        f(x) = prod(x);       // init definition calls prod
        f(x) += prod(x) * r;  // update definition also calls prod

        f.update(0).eager_inline(prod);

        internal_assert(mentions(f.function().definition().values()[0], "prod"))
            << "Stage::eager_inline on update(0) should not touch the init definition\n";
        internal_assert(!mentions(f.function().update(0).values()[0], "prod"))
            << "Stage::eager_inline on update(0) should have inlined prod into the update\n";

        // Semantics preserved: f(x) = (x+1) + sum_{r=0..3} (x+1)*r = 7*(x+1).
        Buffer<int> fout = f.realize({8});
        for (int i = 0; i < 8; i++) {
            if (fout(i) != 7 * (i + 1)) {
                printf("stage eager_inline mismatch at %d: %d vs %d\n", i, fout(i), 7 * (i + 1));
                return 1;
            }
        }

        // Func-level: targets the init definition only, leaving updates alone.
        Func g{"g"};
        g(x) = prod(x);
        g(x) += prod(x) * r;
        g.eager_inline(prod);
        internal_assert(!mentions(g.function().definition().values()[0], "prod"))
            << "Func::eager_inline should inline prod into the init definition\n";
        internal_assert(mentions(g.function().update(0).values()[0], "prod"))
            << "Func::eager_inline should not touch update definitions\n";
    }

    printf("Success!\n");
    return 0;
}
