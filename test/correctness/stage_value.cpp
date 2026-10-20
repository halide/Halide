#include "Halide.h"

#include <cstdio>
#include <string>

using namespace Halide;
using namespace Halide::Internal;

namespace {

bool calls(const Expr &e, const std::string &name) {
    bool found = false;
    visit_with(e, [&](auto *self, const Call *op) {
        found |= op->name == name;
        self->visit_base(op);
    });
    return found;
}

}  // namespace

int main(int argc, char **argv) {
    Var x("x");
    RDom r(0, 8, "r");
    Param<bool> p("p");

    // The pure and update definitions, as Func::value() and update_value() see them.
    {
        Func f("f");
        f(x) = x + 1;
        f(x) += r * 2;
        if (!equal(Stage(f).value(), f.value()) ||
            !equal(f.update().value(), f.update_value())) {
            printf("Stage::value() disagrees with Func::value()/update_value()\n");
            return 1;
        }
        if (f.update().values().size() != 1 || !equal(f.update().values()[0], f.update_value())) {
            printf("Stage::values() of a single-valued update is not a singleton\n");
            return 1;
        }
    }

    // A specialization's definition is its own: the Stage specialize() returns
    // sees it, and rewriting it leaves the unspecialized definition alone.
    {
        Func g("g"), h("h"), f("f");
        g(x) = x * 3;
        h(x) = x - 1;
        f(x) = 0;
        f(x) += g(r) * h(x);
        Stage s = f.update().specialize(p);
        s.eager_inline({g});
        if (calls(s.value(), g.name()) || !calls(f.update_value(), g.name()) ||
            !calls(f.update().value(), g.name())) {
            printf("Stage::value() does not see the eager_inline() on a specialization\n");
            return 1;
        }
        // specialize() with the same condition refers to the same definition.
        if (!equal(f.update().specialize(p).value(), s.value())) {
            printf("Stage::value() differs between two Stages of one specialization\n");
            return 1;
        }

        // ... including after a rewrite that replaces the whole definition.
        Func intm = s.hoist_invariants();
        if (!calls(s.value(), intm.name()) || calls(f.update().value(), intm.name())) {
            printf("Stage::value() does not see hoist_invariants() on a specialization\n");
            return 1;
        }

        p.set(true);
        Buffer<int> on = f.realize({4});
        p.set(false);
        Buffer<int> off = f.realize({4});
        for (int i = 0; i < 4; i++) {
            const int expected = 3 * 28 * (i - 1);
            if (on(i) != expected || off(i) != expected) {
                printf("f(%d) = %d / %d, expected %d\n", i, on(i), off(i), expected);
                return 1;
            }
        }
    }

    // Tuple-valued definitions.
    {
        Func f("f");
        f(x) = Tuple(x, cast<float>(x));
        f(x) = Tuple(f(x)[0] + r, f(x)[1] * 2.0f);
        Tuple t = f.update().values();
        if (t.size() != 2 || !equal(t[0], f.update_values()[0]) || !equal(t[1], f.update_values()[1])) {
            printf("Stage::values() of a Tuple update is wrong\n");
            return 1;
        }
        if (exceptions_enabled()) {
            bool threw = false;
            try {
                (void)f.update().value();
            } catch (const CompileError &e) {
                threw = std::string(e.what()).find("Stage::values()") != std::string::npos;
            }
            if (!threw) {
                printf("Stage::value() of a Tuple update should be an error\n");
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
