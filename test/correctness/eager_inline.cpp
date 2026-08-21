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

    // Pass the Funcs in "wrong" (callee-before-caller) order: a before b, even
    // though b's body calls a. eager_inline() topologically sorts them, so both
    // are fully folded regardless of the argument order.
    c.eager_inline(a, b);

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

    // A longer chain, also passed in a scrambled order, to exercise the
    // topological sort more thoroughly: each Func's body calls the previous one.
    {
        Func d0{"d0"}, d1{"d1"}, d2{"d2"}, d3{"d3"}, sink{"sink"};
        d0(x) = x + 1;
        d1(x) = d0(x) * 2;    // calls d0
        d2(x) = d1(x) + 3;    // calls d1
        d3(x) = d2(x) * 5;    // calls d2
        sink(x) = d3(x) - 4;  // calls d3

        // Scrambled order (not caller-first): the sort must still order them so
        // every call gets folded.
        sink.eager_inline(d2, d0, d3, d1);

        Expr sink_body = sink.function().definition().values()[0];
        internal_assert(!mentions(sink_body, "d0") && !mentions(sink_body, "d1") &&
                        !mentions(sink_body, "d2") && !mentions(sink_body, "d3"))
            << "eager_inline left residual calls after a scrambled-order chain\n"
            << "Saw: " << sink_body << "\n";

        Buffer<int> sout = sink.realize({8});
        for (int i = 0; i < 8; i++) {
            int ref = (((i + 1) * 2 + 3) * 5) - 4;
            if (sout(i) != ref) {
                printf("eager_inline scrambled-chain mismatch at %d: %d vs %d\n", i, sout(i), ref);
                return 1;
            }
        }
    }

    // Passing a Func that this stage does not call is silently ignored: there
    // are no direct calls to fold, so the definition is left unchanged.
    {
        Func p{"p"}, unrelated{"unrelated"}, q{"q"};
        p(x) = x + 1;
        unrelated(x) = x * 100;  // never referenced by q
        q(x) = p(x) + 2;         // calls p, but not unrelated

        // Mix a reachable Func (p) with an unreachable one (unrelated): p is
        // inlined, unrelated is a no-op rather than an error.
        q.eager_inline(unrelated, p);

        Expr q_body = q.function().definition().values()[0];
        internal_assert(!mentions(q_body, "p"))
            << "eager_inline should have inlined the reachable Func p\n"
            << "Saw: " << q_body << "\n";

        Buffer<int> qout = q.realize({8});
        for (int i = 0; i < 8; i++) {
            if (qout(i) != (i + 1) + 2) {
                printf("eager_inline unreachable-arg mismatch at %d: %d vs %d\n", i, qout(i), (i + 1) + 2);
                return 1;
            }
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
