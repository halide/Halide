#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

bool calls(const Func &g, const Func &f) {
    return find_direct_calls(g.function()).count(f.name()) > 0;
}

}  // namespace

int main(int argc, char **argv) {
    Var x("x");

    // g calls f, h does not, and u is undefined.
    Func f("f"), g("g"), h("h"), u("u"), out("out");
    f(x) = x;
    g(x) = f(x) + 1;
    h(x) = x * 2;
    out(x) = g(x) + h(x);

    Func w = f.in_callers({g, h, u});
    if (!w.defined() || !calls(g, w) || calls(g, f) || calls(h, w)) {
        printf("in_callers() did not wrap f in exactly its caller g\n");
        return 1;
    }

    // None of these calls f: nothing changes.
    size_t wrappers = f.function().wrappers().size();
    Func none = f.in_callers({h, u});
    if (none.defined() || f.function().wrappers().size() != wrappers) {
        printf("in_callers() wrapped f in Funcs that do not call it\n");
        return 1;
    }

    // q calls a only through p, so a's clone goes in p, as for clone_in().
    Func a("a"), p("p"), q("q");
    a(x) = x * 3;
    p(x) = a(x) + 2;
    q(x) = p(x) * 2;
    Func c = a.clone_in_callers({q, h});
    if (!c.defined() || !calls(p, c) || calls(p, a)) {
        printf("clone_in_callers() did not clone a into p\n");
        return 1;
    }

    w.compute_root();
    c.compute_root();
    Buffer<int> r1 = out.realize({10});
    Buffer<int> r2 = q.realize({10});
    for (int i = 0; i < 10; i++) {
        if (r1(i) != i + 1 + 2 * i || r2(i) != (3 * i + 2) * 2) {
            printf("Wrong result at %d: %d, %d\n", i, r1(i), r2(i));
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
