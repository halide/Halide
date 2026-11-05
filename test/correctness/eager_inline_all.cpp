#include "Halide.h"
#include <sstream>
#include <string>

using namespace Halide;

// eager_inline_all() inlines every Func a stage calls, transitively, except
// the given ones, and skips (keeping the calls to) Funcs that eager_inline()
// would reject.

namespace {

// Does the printed form of `e` contain a direct call to Func `name`?
bool mentions(const Expr &e, const std::string &name) {
    std::ostringstream os;
    os << e;
    return os.str().find(name + "(") != std::string::npos;
}

int check(const Expr &e, const Func &f, bool expected) {
    if (mentions(e, f.name()) != expected) {
        std::ostringstream os;
        os << e;
        printf("Expected %s %sto be called in %s\n", f.name().c_str(), expected ? "" : "not ", os.str().c_str());
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Var x{"x"};
    int failures = 0;

    {
        // A chain: f's update calls b, which calls a, which calls the
        // compute_root lut. lut is kept, a and b are inlined.
        Func lut{"lut"}, a{"a"}, b{"b"}, f{"f"};
        RDom r(0, 8);
        lut(x) = x * 3;
        a(x) = lut(x % 4) + 1;
        b(x) = a(x) * 2;
        f(x) = 0;
        f(x) += b(x + r);
        lut.compute_root();
        f.update().eager_inline_all();

        Expr v = f.function().update(0).values()[0];
        failures += check(v, lut, true);
        failures += check(v, a, false);
        failures += check(v, b, false);

        Buffer<int> out = f.realize({4});
        for (int i = 0; i < 4; i++) {
            int ref = 0;
            for (int j = 0; j < 8; j++) {
                ref += ((i + j) % 4 * 3 + 1) * 2;
            }
            if (out(i) != ref) {
                printf("f(%d) = %d instead of %d\n", i, out(i), ref);
                return 1;
            }
        }
    }

    {
        // except: a stays a call, and so does what it calls. The impure g
        // (which has an update) is skipped. The pure definition is untouched
        // when inlining into an update.
        Func a{"a"}, b{"b"}, c{"c"}, g{"g"}, f{"f"};
        c(x) = x;
        a(x) = c(x) + 1;
        g(x) = x;
        g(x) += 1;
        b(x) = a(x) + g(x);
        f(x) = b(x);
        f(x) += b(x);
        f.update().eager_inline_all({a});

        Expr v = f.function().update(0).values()[0];
        failures += check(v, a, true);
        failures += check(v, c, false);
        failures += check(v, g, true);
        failures += check(v, b, false);
        failures += check(f.function().values()[0], b, true);

        // Func::eager_inline_all() targets the pure definition.
        f.eager_inline_all();
        Expr p = f.function().values()[0];
        failures += check(p, a, false);
        failures += check(p, c, false);
        failures += check(p, g, true);

        Buffer<int> out = f.realize({4});
        for (int i = 0; i < 4; i++) {
            int ref = 2 * ((i + 1) + (i + 1));
            if (out(i) != ref) {
                printf("f(%d) = %d instead of %d\n", i, out(i), ref);
                return 1;
            }
        }
    }

    {
        // Calls in a specialization are inlined too, and a Func with no
        // callees is left alone.
        Func a{"a"}, f{"f"};
        Param<bool> p;
        a(x) = x * 2;
        f(x) = a(x);
        f.specialize(p).vectorize(x, 4);
        f.eager_inline_all();
        failures += check(f.function().values()[0], a, false);
        failures += check(f.function().definition().specializations()[0].definition.values()[0], a, false);
        Func k{"k"};
        k(x) = x;
        k.eager_inline_all();

        p.set(true);
        Buffer<int> out = f.realize({8});
        for (int i = 0; i < 8; i++) {
            if (out(i) != i * 2) {
                printf("f(%d) = %d instead of %d\n", i, out(i), i * 2);
                return 1;
            }
        }
    }

    if (failures) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
